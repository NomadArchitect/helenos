/*
 * Copyright (c) 2011 Vojtech Horky
 * Copyright (c) 2011 Jiri Svoboda
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

/** @addtogroup libc
 * @{
 */

#include <assert.h>
#include <errno.h>
#include <fibril_synch.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <async.h>
#include <io/log.h>
#include <ipc/logger.h>
#include <ns.h>

/** Log messages are printed under this name. */
static const char *log_prog_name;

static const char *log_level_names[] = {
	"fatal",
	"error",
	"warn",
	"note",
	"debug",
	"debug2",
	NULL
};

/** IPC session with the logger service. */
static async_sess_t *logger_session;

/** Maximum length of a single log message (in bytes). */
#define MESSAGE_BUFFER_SIZE 4096

static int logger_register(async_sess_t *session, const char *prog_name)
{
	async_exch_t *exchange = async_exchange_begin(session);
	if (exchange == NULL) {
		return ENOMEM;
	}

	aid_t reg_msg = async_send_0(exchange, LOGGER_REGISTER, NULL);
	int rc = async_data_write_start(exchange, prog_name, str_size(prog_name));
	sysarg_t reg_msg_rc;
	async_wait_for(reg_msg, &reg_msg_rc);

	async_exchange_end(exchange);

	if (rc != EOK) {
		return rc;
	}

	return reg_msg_rc;
}

static int logger_message(async_sess_t *session, log_context_t ctx, log_level_t level, const char *message)
{
	async_exch_t *exchange = async_exchange_begin(session);
	if (exchange == NULL) {
		return ENOMEM;
	}

	aid_t reg_msg = async_send_2(exchange, LOGGER_MESSAGE,
	    ctx, level, NULL);
	int rc = async_data_write_start(exchange, message, str_size(message));
	sysarg_t reg_msg_rc;
	async_wait_for(reg_msg, &reg_msg_rc);

	async_exchange_end(exchange);

	/*
	 * Getting ENAK means no-one wants our message. That is not an
	 * error at all.
	 */
	if (rc == ENAK)
		rc = EOK;

	if (rc != EOK) {
		return rc;
	}

	return reg_msg_rc;
}

const char *log_level_str(log_level_t level)
{
	if (level >= LVL_LIMIT)
		return "unknown";
	else
		return log_level_names[level];
}

int log_level_from_str(const char *name, log_level_t *level_out)
{
	log_level_t level = LVL_FATAL;

	while (log_level_names[level] != NULL) {
		if (str_cmp(name, log_level_names[level]) == 0) {
			if (level_out != NULL)
				*level_out = level;
			return EOK;
		}
		level++;
	}

	/* Maybe user specified number directly. */
	char *end_ptr;
	int level_int = strtol(name, &end_ptr, 0);
	if ((end_ptr == name) || (str_length(end_ptr) != 0))
		return EINVAL;
	if (level_int < 0)
		return ERANGE;
	if (level_int >= (int) LVL_LIMIT)
		return ERANGE;

	if (level_out != NULL)
		*level_out = (log_level_t) level_int;

	return EOK;
}

/** Initialize the logging system.
 *
 * @param prog_name	Program name, will be printed as part of message
 * @param level		Minimum message level to print
 */
int log_init(const char *prog_name, log_level_t level)
{
	assert(level < LVL_LIMIT);

	log_prog_name = str_dup(prog_name);
	if (log_prog_name == NULL)
		return ENOMEM;

	logger_session = service_connect_blocking(EXCHANGE_SERIALIZE, SERVICE_LOGGER, LOGGER_INTERFACE_SINK, 0);
	if (logger_session == NULL) {
		return ENOMEM;
	}

	int rc = logger_register(logger_session, log_prog_name);

	return rc;
}

/** Create logging context.
 *
 * This function always returns a valid context.
 */
log_context_t log_context_create(const char *name)
{
	async_exch_t *exchange = async_exchange_begin(logger_session);
	if (exchange == NULL)
		return LOG_CONTEXT_DEFAULT;

	ipc_call_t answer;
	aid_t reg_msg = async_send_0(exchange, LOGGER_CREATE_CONTEXT, &answer);
	int rc = async_data_write_start(exchange, name, str_size(name));
	sysarg_t reg_msg_rc;
	async_wait_for(reg_msg, &reg_msg_rc);

	async_exchange_end(exchange);

	if ((rc != EOK) || (reg_msg_rc != EOK))
		return LOG_CONTEXT_DEFAULT;

	return IPC_GET_ARG1(answer);
}

/** Write an entry to the log.
 *
 * @param level		Message verbosity level. Message is only printed
 *			if verbosity is less than or equal to current
 *			reporting level.
 * @param fmt		Format string (no traling newline).
 */
void log_ctx_msg(log_context_t ctx, log_level_t level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	log_ctx_msgv(ctx, level, fmt, args);
	va_end(args);
}

/** Write an entry to the log (va_list variant).
 *
 * @param level		Message verbosity level. Message is only printed
 *			if verbosity is less than or equal to current
 *			reporting level.
 * @param fmt		Format string (no trailing newline)
 */
void log_ctx_msgv(log_context_t ctx, log_level_t level, const char *fmt, va_list args)
{
	assert(level < LVL_LIMIT);

	char *message_buffer = malloc(MESSAGE_BUFFER_SIZE);
	if (message_buffer == NULL) {
		return;
	}

	vsnprintf(message_buffer, MESSAGE_BUFFER_SIZE, fmt, args);
	logger_message(logger_session, ctx, level, message_buffer);
}

/** @}
 */
