/*
 * Copyright (C) 2006 Jakub Jermar
 * Copyright (C) 2006 Jakub Vana
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <arch/mm/page.h>
#include <genarch/mm/page_ht.h>
#include <mm/asid.h>
#include <arch/mm/asid.h>
#include <arch/types.h>
#include <typedefs.h>
#include <print.h>
#include <mm/page.h>
#include <mm/frame.h>
#include <config.h>
#include <panic.h>
#include <arch/asm.h>
#include <arch/barrier.h>
#include <memstr.h>

static void set_vhpt_environment(void);

/** Initialize ia64 virtual address translation subsystem. */
void page_arch_init(void)
{
	page_operations = &page_ht_operations;
	pk_disable();
	set_vhpt_environment();
}

/** Initialize VHPT and region registers. */
void set_vhpt_environment(void)
{
	region_register rr;
	pta_register pta;	
	int i;
	
	/*
	 * First set up kernel region register.
	 */
	rr.word = rr_read(VRN_KERNEL);
	rr.map.ve = 0;                  /* disable VHPT walker */
	rr.map.ps = PAGE_WIDTH;
	rr.map.rid = ASID_KERNEL;
	rr_write(VRN_KERNEL, rr.word);
	srlz_i();
	srlz_d();
	
	/*
	 * And invalidate the rest of region register.
	 */
	for(i = 0; i < REGION_REGISTERS; i++) {
		/* skip kernel rr */
		if (i == VRN_KERNEL)
			continue;
	
		rr.word == rr_read(i);
		rr.map.ve = 0;		/* disable VHPT walker */
		rr.map.rid = ASID_INVALID;
		rr_write(i, rr.word);
		srlz_i();
		srlz_d();
	}

	/*
	 * Allocate VHPT and invalidate all its entries.
	 */
	page_ht = (pte_t *) frame_alloc(FRAME_KA, VHPT_WIDTH - FRAME_WIDTH, NULL);
	memsetb((__address) page_ht, VHPT_SIZE, 0);
	ht_invalidate_all();	
	
	/*
	 * Set up PTA register.
	 */
	pta.word = pta_read();
	pta.map.ve = 0;                   /* disable VHPT walker */
	pta.map.vf = 1;                   /* large entry format */
	pta.map.size = VHPT_WIDTH;
	pta.map.base = ((__address) page_ht) >> PTA_BASE_SHIFT;
	pta_write(pta.word);
	srlz_i();
	srlz_d();
}

/** Calculate address of collision chain from VPN and ASID.
 *
 * Interrupts must be disabled.
 *
 * @param page Address of virtual page including VRN bits.
 * @param asid Address space identifier.
 *
 * @return Head of VHPT collision chain for page and asid.
 */
pte_t *vhpt_hash(__address page, asid_t asid)
{
	region_register rr_save, rr;
	index_t vrn;
	rid_t rid;
	pte_t *t;

	vrn = page >> VRN_SHIFT;
	rid = ASID2RID(asid, vrn);
	
	rr_save.word = rr_read(vrn);
	if (rr_save.map.rid == rid) {
		/*
		 * The RID is already in place, compute thash and return.
		 */
		t = (pte_t *) thash(page);
		return t;
	}
	
	/*
	 * The RID must be written to some region register.
	 * To speed things up, register indexed by vrn is used.
	 */
	rr.word = rr_save.word;
	rr.map.rid = rid;
	rr_write(vrn, rr.word);
	srlz_i();
	t = (pte_t *) thash(page);
	rr_write(vrn, rr_save.word);
	srlz_i();
	srlz_d();

	return t;
}

/** Compare ASID and VPN against PTE.
 *
 * Interrupts must be disabled.
 *
 * @param page Address of virtual page including VRN bits.
 * @param asid Address space identifier.
 *
 * @return True if page and asid match the page and asid of t, false otherwise.
 */
bool vhpt_compare(__address page, asid_t asid, pte_t *t)
{
	region_register rr_save, rr;	
	index_t vrn;
	rid_t rid;
	bool match;

	ASSERT(t);

	vrn = page >> VRN_SHIFT;
	rid = ASID2RID(asid, vrn);
	
	rr_save.word = rr_read(vrn);
	if (rr_save.map.rid == rid) {
		/*
		 * The RID is already in place, compare ttag with t and return.
		 */
		return ttag(page) == t->present.tag.tag_word;
	}
	
	/*
	 * The RID must be written to some region register.
	 * To speed things up, register indexed by vrn is used.
	 */
	rr.word = rr_save.word;
	rr.map.rid = rid;
	rr_write(vrn, rr.word);
	srlz_i();
	match = (ttag(page) == t->present.tag.tag_word);
	rr_write(vrn, rr_save.word);
	srlz_i();
	srlz_d();

	return match;		
}

/** Set up one VHPT entry.
 *
 * @param t VHPT entry to be set up.
 * @param page Virtual address of the page mapped by the entry.
 * @param asid Address space identifier of the address space to which page belongs.
 * @param frame Physical address of the frame to wich page is mapped.
 * @param flags Different flags for the mapping.
 */
void vhpt_set_record(pte_t *t, __address page, asid_t asid, __address frame, int flags)
{
	region_register rr_save, rr;	
	index_t vrn;
	rid_t rid;
	__u64 tag;

	ASSERT(t);

	vrn = page >> VRN_SHIFT;
	rid = ASID2RID(asid, vrn);
	
	/*
	 * Compute ttag.
	 */
	rr_save.word = rr_read(vrn);
	rr.word = rr_save.word;
	rr.map.rid = rid;
	rr_write(vrn, rr.word);
	srlz_i();
	tag = ttag(page);
	rr_write(vrn, rr_save.word);
	srlz_i();
	srlz_d();
	
	/*
	 * Clear the entry.
	 */
	t->word[0] = 0;
	t->word[1] = 0;
	t->word[2] = 0;
	t->word[3] = 0;
	
	t->present.p = true;
	t->present.ma = (flags & PAGE_CACHEABLE) ? MA_WRITEBACK : MA_UNCACHEABLE;
	t->present.a = false;	/* not accessed */
	t->present.d = false;	/* not dirty */
	t->present.pl = (flags & PAGE_USER) ? PL_USER : PL_KERNEL;
	t->present.ar = (flags & PAGE_WRITE) ? AR_WRITE : AR_READ;
	t->present.ar |= (flags & PAGE_EXEC) ? AR_EXECUTE : 0; 
	t->present.ppn = frame >> PPN_SHIFT;
	t->present.ed = false;	/* exception not deffered */
	t->present.ps = PAGE_WIDTH;
	t->present.key = 0;
	t->present.tag.tag_word = tag;
	t->present.next = NULL;
}
