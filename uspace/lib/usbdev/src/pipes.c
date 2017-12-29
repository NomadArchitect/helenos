/*
 * Copyright (c) 2011 Vojtech Horky
 * Copyright (c) 2011 Jan Vesely
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
/** @addtogroup libusbdev
 * @{
 */
/** @file
 * USB endpoint pipes functions.
 */
#include <usb/dev/pipes.h>
#include <usb/dev/request.h>
#include <usb/usb.h>
#include <usbhc_iface.h>

#include <assert.h>
#include <async.h>
#include <errno.h>
#include <mem.h>

/** Try to clear endpoint halt of default control pipe.
 *
 * @param pipe Pipe for control endpoint zero.
 */
static void clear_self_endpoint_halt(usb_pipe_t *pipe)
{
	assert(pipe != NULL);

	if (!pipe->auto_reset_halt || (pipe->desc.endpoint_no != 0)) {
		return;
	}

	/* Prevent infinite recursion. */
	pipe->auto_reset_halt = false;
	usb_request_clear_endpoint_halt(pipe, 0);
	pipe->auto_reset_halt = true;
}

/** Request a control read transfer on an endpoint pipe.
 *
 * This function encapsulates all three stages of a control transfer.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[in] setup_buffer Buffer with the setup packet.
 * @param[in] setup_buffer_size Size of the setup packet (in bytes).
 * @param[out] data_buffer Buffer for incoming data.
 * @param[in] data_buffer_size Size of the buffer for incoming data (in bytes).
 * @param[out] data_transfered_size Number of bytes that were actually
 *                                  transfered during the DATA stage.
 * @return Error code.
 */
int usb_pipe_control_read(usb_pipe_t *pipe,
    const void *setup_buffer, size_t setup_buffer_size,
    void *buffer, size_t buffer_size, size_t *transfered_size)
{
	assert(pipe);

	if ((setup_buffer == NULL) || (setup_buffer_size != 8)) {
		return EINVAL;
	}

	if ((buffer == NULL) || (buffer_size == 0)) {
		return EINVAL;
	}

	if ((pipe->desc.direction != USB_DIRECTION_BOTH)
	    || (pipe->desc.transfer_type != USB_TRANSFER_CONTROL)) {
		return EBADF;
	}

	uint64_t setup_packet;
	memcpy(&setup_packet, setup_buffer, 8);

	async_exch_t *exch = async_exchange_begin(pipe->bus_session);
	size_t act_size = 0;
	const int rc = usbhc_read(exch, pipe->desc.endpoint_no, setup_packet, buffer,
	    buffer_size, &act_size);
	async_exchange_end(exch);

	if (rc == ESTALL) {
		clear_self_endpoint_halt(pipe);
	}

	if (rc == EOK && transfered_size != NULL) {
		*transfered_size = act_size;
	}

	return rc;
}

/** Request a control write transfer on an endpoint pipe.
 *
 * This function encapsulates all three stages of a control transfer.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[in] setup_buffer Buffer with the setup packet.
 * @param[in] setup_buffer_size Size of the setup packet (in bytes).
 * @param[in] data_buffer Buffer with data to be sent.
 * @param[in] data_buffer_size Size of the buffer with outgoing data (in bytes).
 * @return Error code.
 */
int usb_pipe_control_write(usb_pipe_t *pipe,
    const void *setup_buffer, size_t setup_buffer_size,
    const void *buffer, size_t buffer_size)
{
	assert(pipe);

	if ((setup_buffer == NULL) || (setup_buffer_size != 8)) {
		return EINVAL;
	}

	if ((buffer == NULL) && (buffer_size > 0)) {
		return EINVAL;
	}

	if ((buffer != NULL) && (buffer_size == 0)) {
		return EINVAL;
	}

	if ((pipe->desc.direction != USB_DIRECTION_BOTH)
	    || (pipe->desc.transfer_type != USB_TRANSFER_CONTROL)) {
		return EBADF;
	}

	uint64_t setup_packet;
	memcpy(&setup_packet, setup_buffer, 8);

	async_exch_t *exch = async_exchange_begin(pipe->bus_session);
	const int rc = usbhc_write(exch,
	    pipe->desc.endpoint_no, setup_packet, buffer, buffer_size);
	async_exchange_end(exch);

	if (rc == ESTALL) {
		clear_self_endpoint_halt(pipe);
	}

	return rc;
}

/** Request a read (in) transfer on an endpoint pipe.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[out] buffer Buffer where to store the data.
 * @param[in] size Size of the buffer (in bytes).
 * @param[out] size_transfered Number of bytes that were actually transfered.
 * @return Error code.
 */
int usb_pipe_read(usb_pipe_t *pipe,
    void *buffer, size_t size, size_t *size_transfered)
{
	assert(pipe);

	if (buffer == NULL) {
		return EINVAL;
	}

	if (size == 0) {
		return EINVAL;
	}

	if (pipe->desc.direction != USB_DIRECTION_IN) {
		return EBADF;
	}

	if (pipe->desc.transfer_type == USB_TRANSFER_CONTROL) {
		return EBADF;
	}

	async_exch_t *exch;
	if (pipe->desc.transfer_type == USB_TRANSFER_ISOCHRONOUS)
		exch = async_exchange_begin(pipe->isoch_session);
	else
	 	exch = async_exchange_begin(pipe->bus_session);
	size_t act_size = 0;
	const int rc =
	    usbhc_read(exch, pipe->desc.endpoint_no, 0, buffer, size, &act_size);
	async_exchange_end(exch);

	if (rc == EOK && size_transfered != NULL) {
		*size_transfered = act_size;
	}

	return rc;
}

/** Request a write (out) transfer on an endpoint pipe.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[in] buffer Buffer with data to transfer.
 * @param[in] size Size of the buffer (in bytes).
 * @return Error code.
 */
int usb_pipe_write(usb_pipe_t *pipe, const void *buffer, size_t size)
{
	assert(pipe);

	if (buffer == NULL || size == 0) {
		return EINVAL;
	}

	if (pipe->desc.direction != USB_DIRECTION_OUT) {
		return EBADF;
	}

	if (pipe->desc.transfer_type == USB_TRANSFER_CONTROL) {
		return EBADF;
	}

	async_exch_t *exch;
	if (pipe->desc.transfer_type == USB_TRANSFER_ISOCHRONOUS)
		exch = async_exchange_begin(pipe->isoch_session);
	else
	 	exch = async_exchange_begin(pipe->bus_session);

	const int rc = usbhc_write(exch, pipe->desc.endpoint_no, 0, buffer, size);
	async_exchange_end(exch);
	return rc;
}

/** Setup isochronous session for isochronous communication.
 *  Isochronous endpoints need a different session as they might block while waiting for data.
 *
 * @param pipe Endpoint pipe being initialized.
 * @return Error code.
 */
static int usb_isoch_session_initialize(usb_pipe_t *pipe) {
	devman_handle_t handle;

	async_exch_t *exch = async_exchange_begin(pipe->bus_session);
	if (!exch)
		return EPARTY;

	int ret = usb_get_my_device_handle(exch, &handle);

	async_exchange_end(exch);
	if (ret != EOK)
		return ret;

	pipe->isoch_session = usb_dev_connect(handle);
	if (!pipe->isoch_session)
		return ENAK;

	return EOK;
}

/** Initialize USB endpoint pipe.
 *
 * @param pipe Endpoint pipe to be initialized.
 * @param endpoint_no Endpoint number (in USB 1.1 in range 0 to 15).
 * @param transfer_type Transfer type (e.g. interrupt or bulk).
 * @param max_packet_size Maximum packet size in bytes.
 * @param direction Endpoint direction (in/out).
 * @return Error code.
 */
int usb_pipe_initialize(usb_pipe_t *pipe, usb_endpoint_t endpoint_no,
    usb_transfer_type_t transfer_type, size_t max_packet_size,
    usb_direction_t direction, unsigned packets,
    unsigned max_burst, unsigned max_streams, unsigned bytes_per_interval,
	unsigned mult, usb_dev_session_t *bus_session)
{
	int ret = EOK;
	// FIXME: refactor this function PLEASE
	assert(pipe);

	pipe->desc.endpoint_no = endpoint_no;
	pipe->desc.transfer_type = transfer_type;
	pipe->desc.packets = packets;
	pipe->desc.max_packet_size = max_packet_size;
	pipe->desc.direction = direction;
	pipe->desc.usb3.max_burst = max_burst;
	pipe->desc.usb3.max_streams = max_streams;
	pipe->desc.usb3.mult = mult;
	pipe->desc.usb3.bytes_per_interval = bytes_per_interval;
	pipe->auto_reset_halt = false;
	pipe->bus_session = bus_session;

	// TODO: hardcoded, remake to receive from device descriptors
	pipe->desc.interval = 14;

	if (transfer_type == USB_TRANSFER_ISOCHRONOUS) {
		ret = usb_isoch_session_initialize(pipe);
	}

	return ret;
}

/** Initialize USB endpoint pipe as the default zero control pipe.
 *
 * @param pipe Endpoint pipe to be initialized.
 * @return Error code.
 */
int usb_pipe_initialize_default_control(usb_pipe_t *pipe,
    usb_dev_session_t *bus_session)
{
	assert(pipe);

	const int rc = usb_pipe_initialize(pipe, 0, USB_TRANSFER_CONTROL,
	    CTRL_PIPE_MIN_PACKET_SIZE, USB_DIRECTION_BOTH, 1, 0, 0, 0, 0, bus_session);

	pipe->auto_reset_halt = true;

	return rc;
}

/** Register endpoint with the host controller.
 *
 * @param pipe Pipe to be registered.
 * @param interval Polling interval.
 * @return Error code.
 */
int usb_pipe_register(usb_pipe_t *pipe, unsigned interval)
{
	assert(pipe);
	assert(pipe->bus_session);

	pipe->desc.usb2.polling_interval = interval;
	async_exch_t *exch = async_exchange_begin(pipe->bus_session);
	if (!exch)
		return ENOMEM;

	const int ret = usbhc_register_endpoint(exch, &pipe->desc);

	async_exchange_end(exch);
	return ret;
}

/** Revert endpoint registration with the host controller.
 *
 * @param pipe Pipe to be unregistered.
 * @return Error code.
 */
int usb_pipe_unregister(usb_pipe_t *pipe)
{
	assert(pipe);
	assert(pipe->bus_session);
	async_exch_t *exch = async_exchange_begin(pipe->bus_session);
	if (!exch)
		return ENOMEM;

	const int ret = usbhc_unregister_endpoint(exch, &pipe->desc);

	async_exchange_end(exch);
	return ret;
}

/**
 * @}
 */
