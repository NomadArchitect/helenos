/*
 * Copyright (c) 2007 Michal Kebrt, Petr Stepan
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

/** @addtogroup arm32gxemul
 * @{
 */
/** @file
 *  @brief GXemul drivers.
 */

#include <arch/exception.h>
#include <arch/mach/testarm/testarm.h>
#include <mm/page.h>
#include <genarch/fb/fb.h>
#include <genarch/fb/visuals.h>
#include <genarch/drivers/dsrln/dsrlnin.h>
#include <genarch/drivers/dsrln/dsrlnout.h>
#include <genarch/srln/srln.h>
#include <console/console.h>
#include <ddi/irq.h>
#include <ddi/device.h>
#include <config.h>
#include <sysinfo/sysinfo.h>
#include <interrupt.h>
#include <print.h>


void *gxemul_kbd;
void *gxemul_rtc;
void *gxemul_irqc;
static irq_t gxemul_timer_irq;

struct arm_machine_ops machine_ops = {
	MACHINE_GENFUNC,
	MACHINE_GENFUNC,
	gxemul_init,
	gxemul_timer_irq_start,
	gxemul_cpu_halt,
	gxemul_get_memory_size,
	gxemul_fb_init,
	gxemul_irq_exception,
	gxemul_get_fb_address,
	gxemul_frame_init,
	gxemul_output_init,
	gxemul_input_init
};

void gxemul_init(void)
{
	gxemul_kbd = (void *) hw_map(GXEMUL_KBD_ADDRESS, PAGE_SIZE);
	gxemul_rtc = (void *) hw_map(GXEMUL_RTC_ADDRESS, PAGE_SIZE);
	gxemul_irqc = (void *) hw_map(GXEMUL_IRQC_ADDRESS, PAGE_SIZE);
}

void gxemul_fb_init(void)
{
	fb_properties_t prop = {
		.addr = GXEMUL_FB_ADDRESS,
		.offset = 0,
		.x = 640,
		.y = 480,
		.scan = 1920,
		.visual = VISUAL_BGR_8_8_8,
	};
	fb_init(&prop);
}

void gxemul_output_init(void)
{
	dsrlnout_init((ioport8_t *) gxemul_kbd);
}

void gxemul_input_init(void)
{
#ifdef CONFIG_ARM_KBD
	/*
	 * Initialize the GXemul keyboard port. Then initialize the serial line
	 * module and connect it to the GXemul keyboard.
	 */
	dsrlnin_instance_t *dsrlnin_instance
	    = dsrlnin_init((dsrlnin_t *) gxemul_kbd, GXEMUL_KBD_IRQ);
	if (dsrlnin_instance) {
		srln_instance_t *srln_instance = srln_init();
		if (srln_instance) {
			indev_t *sink = stdin_wire();
			indev_t *srln = srln_wire(srln_instance, sink);
			dsrlnin_wire(dsrlnin_instance, srln);
		}
	}

	/*
	 * This is the necessary evil until the userspace driver is entirely
	 * self-sufficient.
	 */
	sysinfo_set_item_val("kbd", NULL, true);
	sysinfo_set_item_val("kbd.inr", NULL, GXEMUL_KBD_IRQ);
	sysinfo_set_item_val("kbd.address.virtual", NULL, (unative_t) gxemul_kbd);
#endif
}

/** Starts gxemul Real Time Clock device, which asserts regular interrupts.
 *
 * @param frequency Interrupts frequency (0 disables RTC).
 */
static void gxemul_timer_start(uint32_t frequency)
{
	*((uint32_t *) (gxemul_rtc + GXEMUL_RTC_FREQ_OFFSET))
	    = frequency;
}

static irq_ownership_t gxemul_timer_claim(irq_t *irq)
{
	return IRQ_ACCEPT;
}

/** Timer interrupt handler.
 *
 * @param irq Interrupt information.
 * @param arg Not used.
 */
static void gxemul_timer_irq_handler(irq_t *irq)
{
	/*
	* We are holding a lock which prevents preemption.
	* Release the lock, call clock() and reacquire the lock again.
	*/
	spinlock_unlock(&irq->lock);
	clock();
	spinlock_lock(&irq->lock);

	/* acknowledge tick */
	*((uint32_t *) (gxemul_rtc + GXEMUL_RTC_ACK_OFFSET))
	    = 0;
}

/** Initializes and registers timer interrupt handler. */
static void gxemul_timer_irq_init(void)
{
        irq_initialize(&gxemul_timer_irq);
        gxemul_timer_irq.devno = device_assign_devno();
        gxemul_timer_irq.inr = GXEMUL_TIMER_IRQ;
        gxemul_timer_irq.claim = gxemul_timer_claim;
        gxemul_timer_irq.handler = gxemul_timer_irq_handler;

        irq_register(&gxemul_timer_irq);
}


/** Starts timer.
 *
 * Initiates regular timer interrupts after initializing
 * corresponding interrupt handler.
 */
void gxemul_timer_irq_start(void)
{
        gxemul_timer_irq_init();
        gxemul_timer_start(GXEMUL_TIMER_FREQ);
}

/** Returns the size of emulated memory.
 *
 * @return Size in bytes.
 */
uintptr_t gxemul_get_memory_size(void)
{
        return  *((uintptr_t *) (GXEMUL_MP_ADDRESS + GXEMUL_MP_MEMSIZE_OFFSET));
}


/** Returns the mask of active interrupts. */
static inline uint32_t gxemul_irqc_get_sources(void)
{
	return *((uint32_t *) gxemul_irqc);
}

/** Interrupt Exception handler.
 *
 * Determines the sources of interrupt and calls their handlers.
 */
void gxemul_irq_exception(int exc_no, istate_t *istate)
{
	uint32_t sources = gxemul_irqc_get_sources();
	unsigned int i;

	for (i = 0; i < GXEMUL_IRQC_MAX_IRQ; i++) {
		if (sources & (1 << i)) {
			irq_t *irq = irq_dispatch_and_lock(i);
			if (irq) {
				/* The IRQ handler was found. */
				irq->handler(irq);
				spinlock_unlock(&irq->lock);
			} else {
				/* Spurious interrupt.*/
				printf("cpu%d: spurious interrupt (inum=%d)\n",
				    CPU->id, i);
			}
		}
	}
}

void gxemul_cpu_halt(void)
{
	*((char *) (gxemul_kbd + GXEMUL_HALT_OFFSET)) = 0;
}

void gxemul_frame_init(void)
{
}

uintptr_t gxemul_get_fb_address()
{
	return ((uintptr_t)GXEMUL_FB_ADDRESS);
}


/** @}
 */
