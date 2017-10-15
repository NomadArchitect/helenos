/*
 * Copyright (c) 2012 Jan Vesely
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

/** @addtogroup libusbhost
 * @{
 */
/** @file
 *
 */

#ifndef LIBUSBHOST_HOST_DDF_HELPERS_H
#define LIBUSBHOST_HOST_DDF_HELPERS_H

#include <usb/host/hcd.h>
#include <usb/host/bus.h>
#include <usb/usb.h>

#include <ddf/driver.h>
#include <ddf/interrupt.h>
#include <device/hw_res_parsed.h>

typedef int (*driver_init_t)(hcd_t *, const hw_res_list_parsed_t *);
typedef int (*irq_code_gen_t)(irq_code_t *, hcd_t *, const hw_res_list_parsed_t *);
typedef int (*claim_t)(hcd_t *, ddf_dev_t *);
typedef int (*driver_start_t)(hcd_t *, bool irq);
typedef int (*setup_root_hub_t)(hcd_t *, ddf_dev_t *);

typedef void (*driver_stop_t)(hcd_t *);
typedef void (*driver_fini_t)(hcd_t *);

/**
 * All callbacks are optional.
 */
typedef struct {
	hcd_ops_t ops;
	usb_speed_t hc_speed;
	const char *name;

	interrupt_handler_t *irq_handler;  /**< Handler of IRQ. Do have generic implementation. */

	/* Initialization sequence: */
	driver_init_t init;                /**< Initialize internal structures, memory */
	claim_t claim;                     /**< Claim device from BIOS */
	irq_code_gen_t irq_code_gen;       /**< Generate IRQ handling code */
	driver_start_t start;              /**< Start the HC */
	setup_root_hub_t setup_root_hub;   /**< Setup the root hub */

	/* Destruction sequence: */
	driver_stop_t stop;                /**< Stop the HC (counterpart of start) */
	driver_fini_t fini;                /**< Destroy internal structures (counterpart of init) */
} ddf_hc_driver_t;

int hcd_ddf_add_hc(ddf_dev_t *device, const ddf_hc_driver_t *driver);

int hcd_ddf_setup_hc(ddf_dev_t *device);
void hcd_ddf_clean_hc(ddf_dev_t *device);

int hcd_setup_virtual_root_hub(hcd_t *, ddf_dev_t *);

hcd_t *dev_to_hcd(ddf_dev_t *dev);

int hcd_ddf_enable_interrupts(ddf_dev_t *device);
int hcd_ddf_get_registers(ddf_dev_t *device, hw_res_list_parsed_t *hw_res);
int hcd_ddf_setup_interrupts(ddf_dev_t *device,
    const hw_res_list_parsed_t *hw_res,
    interrupt_handler_t handler,
    irq_code_gen_t gen_irq_code);
void ddf_hcd_gen_irq_handler(ipc_callid_t iid, ipc_call_t *call, ddf_dev_t *dev);

/* For xHCI, we need to drive the roothub without roothub having assigned an
 * address. Thus we cannot create function for it, and we have to carry the
 * usb_dev_t somewhere.
 *
 * This is sort of hacky, but at least does not expose the internals of ddf_helpers.
 */
typedef struct hcd_roothub hcd_roothub_t;

hcd_roothub_t *hcd_roothub_create(hcd_t *, ddf_dev_t *, usb_speed_t);
int hcd_roothub_new_device(hcd_roothub_t *, unsigned port);

#endif

/**
 * @}
 */
