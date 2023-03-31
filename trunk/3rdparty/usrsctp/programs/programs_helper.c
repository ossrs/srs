/*-
 * Copyright (c) 2019 -2020 Felix Weinrank
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <usrsctp.h>

#ifndef _WIN32
#include <sys/time.h>
#include <arpa/inet.h>
#else
#include <sys/types.h>
#include <sys/timeb.h>
#include <io.h>
#endif

#include "programs_helper.h"

#define DEFAULT_TARGET stdout;

static FILE *debug_target = NULL;

#ifdef _WIN32
static void
gettimeofday(struct timeval *tv, void *ignore)
{
	FILETIME filetime;
	ULARGE_INTEGER ularge;

	GetSystemTimeAsFileTime(&filetime);
	ularge.LowPart = filetime.dwLowDateTime;
	ularge.HighPart = filetime.dwHighDateTime;
	/* Change base from Jan 1 1601 00:00:00 to Jan 1 1970 00:00:00 */
#if defined(__MINGW32__)
	ularge.QuadPart -= 116444736000000000ULL;
#else
	ularge.QuadPart -= 116444736000000000UI64;
#endif
	/*
	 * ularge.QuadPart is now the number of 100-nanosecond intervals
	 * since Jan 1 1970 00:00:00.
	 */
#if defined(__MINGW32__)
	tv->tv_sec = (long)(ularge.QuadPart / 10000000ULL);
	tv->tv_usec = (long)((ularge.QuadPart % 10000000ULL) / 10ULL);
#else
	tv->tv_sec = (long)(ularge.QuadPart / 10000000UI64);
	tv->tv_usec = (long)((ularge.QuadPart % 10000000UI64) / 10UI64);
#endif
}
#endif

void
debug_set_target(FILE *fp) {
	debug_target = fp;
}

void
debug_printf_clean(const char *format, ...) {
	char charbuf[1024];
	va_list ap;

	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	va_start(ap, format);
	if (vsnprintf(charbuf, 1024, format, ap) < 0) {
		charbuf[0] = '\0';
	}
	va_end(ap);

	fprintf(debug_target, "%s", charbuf);
	fflush(debug_target);
}

void
debug_printf(const char *format, ...) {
	va_list ap;
	char charbuf[1024];
	static struct timeval time_main;
	struct timeval time_now;
	struct timeval time_delta;

	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	if (time_main.tv_sec == 0  && time_main.tv_usec == 0) {
		gettimeofday(&time_main, NULL);
	}

	gettimeofday(&time_now, NULL);
	timersub(&time_now, &time_main, &time_delta);

	va_start(ap, format);
	if (vsnprintf(charbuf, 1024, format, ap) < 0) {
		charbuf[0] = '\0';
	}
	va_end(ap);

	fprintf(debug_target, "[P][%u.%03u] %s", (unsigned int) time_delta.tv_sec, (unsigned int) time_delta.tv_usec / 1000, charbuf);
	fflush(debug_target);
}

void
debug_printf_stack(const char *format, ...)
{
	va_list ap;
	char charbuf[1024];
	static struct timeval time_main;
	struct timeval time_now;
	struct timeval time_delta;

	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	if (time_main.tv_sec == 0  && time_main.tv_usec == 0) {
		gettimeofday(&time_main, NULL);
	}

	gettimeofday(&time_now, NULL);
	timersub(&time_now, &time_main, &time_delta);

	va_start(ap, format);
	if (vsnprintf(charbuf, 1024, format, ap) < 0) {
		charbuf[0] = '\0';
	}
	va_end(ap);

	fprintf(debug_target, "[S][%u.%03u] %s", (unsigned int) time_delta.tv_sec, (unsigned int) time_delta.tv_usec / 1000, charbuf);
	fflush(debug_target);
}

static void
handle_association_change_event(struct sctp_assoc_change *sac)
{
	unsigned int i, n;

	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	fprintf(debug_target, "Association change ");
	switch (sac->sac_state) {
	case SCTP_COMM_UP:
		fprintf(debug_target, "SCTP_COMM_UP");
		break;
	case SCTP_COMM_LOST:
		fprintf(debug_target, "SCTP_COMM_LOST");
		break;
	case SCTP_RESTART:
		fprintf(debug_target, "SCTP_RESTART");
		break;
	case SCTP_SHUTDOWN_COMP:
		fprintf(debug_target, "SCTP_SHUTDOWN_COMP");
		break;
	case SCTP_CANT_STR_ASSOC:
		fprintf(debug_target, "SCTP_CANT_STR_ASSOC");
		break;
	default:
		fprintf(debug_target, "UNKNOWN");
		break;
	}
	fprintf(debug_target, ", streams (in/out) = (%u/%u)",
	       sac->sac_inbound_streams, sac->sac_outbound_streams);
	n = sac->sac_length - sizeof(struct sctp_assoc_change);
	if (((sac->sac_state == SCTP_COMM_UP) ||
	     (sac->sac_state == SCTP_RESTART)) && (n > 0)) {
		fprintf(debug_target, ", supports");
		for (i = 0; i < n; i++) {
			switch (sac->sac_info[i]) {
			case SCTP_ASSOC_SUPPORTS_PR:
				fprintf(debug_target, " PR");
				break;
			case SCTP_ASSOC_SUPPORTS_AUTH:
				fprintf(debug_target, " AUTH");
				break;
			case SCTP_ASSOC_SUPPORTS_ASCONF:
				fprintf(debug_target, " ASCONF");
				break;
			case SCTP_ASSOC_SUPPORTS_MULTIBUF:
				fprintf(debug_target, " MULTIBUF");
				break;
			case SCTP_ASSOC_SUPPORTS_RE_CONFIG:
				fprintf(debug_target, " RE-CONFIG");
				break;
			case SCTP_ASSOC_SUPPORTS_INTERLEAVING:
				fprintf(debug_target, " INTERLEAVING");
				break;
			default:
				fprintf(debug_target, " UNKNOWN(0x%02x)", sac->sac_info[i]);
				break;
			}
		}
	} else if (((sac->sac_state == SCTP_COMM_LOST) ||
	            (sac->sac_state == SCTP_CANT_STR_ASSOC)) && (n > 0)) {
		fprintf(debug_target, ", ABORT =");
		for (i = 0; i < n; i++) {
			fprintf(debug_target, " 0x%02x", sac->sac_info[i]);
		}
	}
	fprintf(debug_target, ".\n");
	return;
}

static void
handle_peer_address_change_event(struct sctp_paddr_change *spc)
{
	char addr_buf[INET6_ADDRSTRLEN];
	const char *addr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	struct sockaddr_conn *sconn;

	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	switch (spc->spc_aaddr.ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)&spc->spc_aaddr;
		addr = inet_ntop(AF_INET, &sin->sin_addr, addr_buf, INET_ADDRSTRLEN);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&spc->spc_aaddr;
		addr = inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf, INET6_ADDRSTRLEN);
		break;
	case AF_CONN:
		sconn = (struct sockaddr_conn *)&spc->spc_aaddr;
#ifdef _WIN32
		if (_snprintf(addr_buf, INET6_ADDRSTRLEN, "%p", sconn->sconn_addr) < 0) {
#else
		if (snprintf(addr_buf, INET6_ADDRSTRLEN, "%p", sconn->sconn_addr) < 0) {
#endif
			addr_buf[0] = '\0';
		}
		addr = addr_buf;
		break;
	default:
#ifdef _WIN32
		if (_snprintf(addr_buf, INET6_ADDRSTRLEN, "Unknown family %d", spc->spc_aaddr.ss_family) < 0) {
#else
		if (snprintf(addr_buf, INET6_ADDRSTRLEN, "Unknown family %d", spc->spc_aaddr.ss_family) < 0) {
#endif
			addr_buf[0] = '\0';
		}
		addr = addr_buf;
		break;
	}
	fprintf(debug_target, "Peer address %s is now ", addr);
	switch (spc->spc_state) {
	case SCTP_ADDR_AVAILABLE:
		fprintf(debug_target, "SCTP_ADDR_AVAILABLE");
		break;
	case SCTP_ADDR_UNREACHABLE:
		fprintf(debug_target, "SCTP_ADDR_UNREACHABLE");
		break;
	case SCTP_ADDR_REMOVED:
		fprintf(debug_target, "SCTP_ADDR_REMOVED");
		break;
	case SCTP_ADDR_ADDED:
		fprintf(debug_target, "SCTP_ADDR_ADDED");
		break;
	case SCTP_ADDR_MADE_PRIM:
		fprintf(debug_target, "SCTP_ADDR_MADE_PRIM");
		break;
	case SCTP_ADDR_CONFIRMED:
		fprintf(debug_target, "SCTP_ADDR_CONFIRMED");
		break;
	default:
		fprintf(debug_target, "UNKNOWN");
		break;
	}
	fprintf(debug_target, " (error = 0x%08x).\n", spc->spc_error);
	return;
}

static void
handle_send_failed_event(struct sctp_send_failed_event *ssfe)
{
	size_t i, n;

	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	if (ssfe->ssfe_flags & SCTP_DATA_UNSENT) {
		fprintf(debug_target, "Unsent ");
	}
	if (ssfe->ssfe_flags & SCTP_DATA_SENT) {
		fprintf(debug_target, "Sent ");
	}
	if (ssfe->ssfe_flags & ~(SCTP_DATA_SENT | SCTP_DATA_UNSENT)) {
		fprintf(debug_target, "(flags = %x) ", ssfe->ssfe_flags);
	}
	fprintf(debug_target, "message with PPID = %u, SID = %u, flags: 0x%04x due to error = 0x%08x",
	       (uint32_t)ntohl(ssfe->ssfe_info.snd_ppid), ssfe->ssfe_info.snd_sid,
	       ssfe->ssfe_info.snd_flags, ssfe->ssfe_error);
	n = ssfe->ssfe_length - sizeof(struct sctp_send_failed_event);
	for (i = 0; i < n; i++) {
		fprintf(debug_target, " 0x%02x", ssfe->ssfe_data[i]);
	}
	fprintf(debug_target, ".\n");
	return;
}

static void
handle_adaptation_indication(struct sctp_adaptation_event *sai)
{
	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	fprintf(debug_target, "Adaptation indication: %x.\n", sai-> sai_adaptation_ind);
	return;
}

static void
handle_shutdown_event(struct sctp_shutdown_event *sse)
{
	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	fprintf(debug_target, "Shutdown event.\n");
	/* XXX: notify all channels. */
	return;
}

static void
handle_stream_reset_event(struct sctp_stream_reset_event *strrst)
{
	uint32_t n, i;

	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	n = (strrst->strreset_length - sizeof(struct sctp_stream_reset_event)) / sizeof(uint16_t);
	fprintf(debug_target, "Stream reset event: flags = %x, ", strrst->strreset_flags);
	if (strrst->strreset_flags & SCTP_STREAM_RESET_INCOMING_SSN) {
		if (strrst->strreset_flags & SCTP_STREAM_RESET_OUTGOING_SSN) {
			fprintf(debug_target, "incoming/");
		}
		fprintf(debug_target, "incoming ");
	}
	if (strrst->strreset_flags & SCTP_STREAM_RESET_OUTGOING_SSN) {
		fprintf(debug_target, "outgoing ");
	}
	fprintf(debug_target, "stream ids = ");
	for (i = 0; i < n; i++) {
		if (i > 0) {
			fprintf(debug_target, ", ");
		}
		fprintf(debug_target, "%d", strrst->strreset_stream_list[i]);
	}
	fprintf(debug_target, ".\n");
	return;
}

static void
handle_stream_change_event(struct sctp_stream_change_event *strchg)
{
	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	fprintf(debug_target, "Stream change event: streams (in/out) = (%u/%u), flags = %x.\n",
	       strchg->strchange_instrms, strchg->strchange_outstrms, strchg->strchange_flags);
	return;
}

static void
handle_remote_error_event(struct sctp_remote_error *sre)
{
	size_t i, n;

	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	n = sre->sre_length - sizeof(struct sctp_remote_error);
	fprintf(debug_target, "Remote Error (error = 0x%04x): ", sre->sre_error);
	for (i = 0; i < n; i++) {
		fprintf(debug_target, " 0x%02x", sre-> sre_data[i]);
	}
	fprintf(debug_target, ".\n");
	return;
}

void
handle_notification(union sctp_notification *notif, size_t n)
{
	if (notif->sn_header.sn_length != (uint32_t)n) {
		return;
	}

	if (debug_target == NULL) {
		debug_target = DEFAULT_TARGET;
	}

	fprintf(debug_target, "handle_notification : ");

	switch (notif->sn_header.sn_type) {
	case SCTP_ASSOC_CHANGE:
		fprintf(debug_target, "SCTP_ASSOC_CHANGE\n");
		handle_association_change_event(&(notif->sn_assoc_change));
		break;
	case SCTP_PEER_ADDR_CHANGE:
		fprintf(debug_target, "SCTP_PEER_ADDR_CHANGE\n");
		handle_peer_address_change_event(&(notif->sn_paddr_change));
		break;
	case SCTP_REMOTE_ERROR:
		fprintf(debug_target, "SCTP_REMOTE_ERROR\n");
		handle_remote_error_event(&(notif->sn_remote_error));
		break;
	case SCTP_SHUTDOWN_EVENT:
		fprintf(debug_target, "SCTP_SHUTDOWN_EVENT\n");
		handle_shutdown_event(&(notif->sn_shutdown_event));
		break;
	case SCTP_ADAPTATION_INDICATION:
		fprintf(debug_target, "SCTP_ADAPTATION_INDICATION\n");
		handle_adaptation_indication(&(notif->sn_adaptation_event));
		break;
	case SCTP_PARTIAL_DELIVERY_EVENT:
		fprintf(debug_target, "SCTP_PARTIAL_DELIVERY_EVENT\n");
		break;
	case SCTP_AUTHENTICATION_EVENT:
		fprintf(debug_target, "SCTP_AUTHENTICATION_EVENT\n");
		break;
	case SCTP_SENDER_DRY_EVENT:
		fprintf(debug_target, "SCTP_SENDER_DRY_EVENT\n");
		break;
	case SCTP_NOTIFICATIONS_STOPPED_EVENT:
		fprintf(debug_target, "SCTP_NOTIFICATIONS_STOPPED_EVENT\n");
		break;
	case SCTP_SEND_FAILED_EVENT:
		fprintf(debug_target, "SCTP_SEND_FAILED_EVENT\n");
		handle_send_failed_event(&(notif->sn_send_failed_event));
		break;
	case SCTP_STREAM_RESET_EVENT:
		fprintf(debug_target, "SCTP_STREAM_RESET_EVENT\n");
		handle_stream_reset_event(&(notif->sn_strreset_event));
		break;
	case SCTP_ASSOC_RESET_EVENT:
		fprintf(debug_target, "SCTP_ASSOC_RESET_EVENT\n");
		break;
	case SCTP_STREAM_CHANGE_EVENT:
		fprintf(debug_target, "SCTP_STREAM_CHANGE_EVENT\n");
		handle_stream_change_event(&(notif->sn_strchange_event));
		break;
	default:
		break;
	}
}
