/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

struct parspec;

typedef int tweak_t(struct vsb *, const struct parspec *, const char *arg);

/* Sentinel for the arg position of tweak_t to ask for JSON formatting. */
extern const char * const JSON_FMT;

struct parspec {
	const char	*name;
	tweak_t		*func;
	volatile void	*priv;
	const char	*min;
	const char	*max;
	const char	*def;
	const char	*units;
	const char	*descr;
	unsigned	 flags;
#define DELAYED_EFFECT		(1<<0)
#define EXPERIMENTAL		(1<<1)
#define MUST_RESTART		(1<<2)
#define MUST_RELOAD		(1<<3)
#define WIZARD			(1<<4)
#define PROTECTED		(1<<5)
#define OBJ_STICKY		(1<<6)
#define ONLY_ROOT		(1<<7)
#define NOT_IMPLEMENTED		(1<<8)
#define PLATFORM_DEPENDENT	(1<<9)
#define BUILD_OPTIONS		(1<<10)
#define TYPE_TIMEOUT		(1<<11)

#define DOCS_FLAGS	(NOT_IMPLEMENTED|PLATFORM_DEPENDENT|BUILD_OPTIONS)

	const char	*dyn_min_reason;
	const char	*dyn_max_reason;
	const char	*dyn_def_reason;
	char		*dyn_min;
	char		*dyn_max;
	char		*dyn_def;
};

tweak_t tweak_alias;
tweak_t tweak_boolean;
tweak_t tweak_bytes;
tweak_t tweak_bytes_u;
tweak_t tweak_double;
tweak_t tweak_duration;
tweak_t tweak_debug;
tweak_t tweak_experimental;
tweak_t tweak_feature;
tweak_t tweak_poolparam;
tweak_t tweak_storage;
tweak_t tweak_string;
tweak_t tweak_thread_pool_min;
tweak_t tweak_thread_pool_max;
tweak_t tweak_timeout;
tweak_t tweak_uint;
tweak_t tweak_uint_orzero;
tweak_t tweak_vcc_feature;
tweak_t tweak_vsl_buffer;
tweak_t tweak_vsl_mask;
tweak_t tweak_vsl_reclen;
