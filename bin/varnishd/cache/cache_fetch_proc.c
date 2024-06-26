/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "vcli_serve.h"

static unsigned fetchfrag;

/*--------------------------------------------------------------------
 * We want to issue the first error we encounter on fetching and
 * suppress the rest.  This function does that.
 *
 * Other code is allowed to look at busyobj->fetch_failed to bail out
 *
 * For convenience, always return VFP_ERROR
 */

enum vfp_status
VFP_Error(struct vfp_ctx *vc, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	if (!vc->failed) {
		va_start(ap, fmt);
		VSLbv(vc->wrk->vsl, SLT_FetchError, fmt, ap);
		va_end(ap);
		vc->failed = 1;
	}
	return (VFP_ERROR);
}

/*--------------------------------------------------------------------
 * Fetch Storage to put object into.
 *
 */

enum vfp_status
VFP_GetStorage(struct vfp_ctx *vc, ssize_t *sz, uint8_t **ptr)
{

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	AN(sz);
	assert(*sz >= 0);
	AN(ptr);

	if (fetchfrag > 0)
		*sz = fetchfrag;

	if (!ObjGetSpace(vc->wrk, vc->oc, sz, ptr)) {
		*sz = 0;
		*ptr = NULL;
		return (VFP_Error(vc, "Could not get storage"));
	}
	assert(*sz > 0);
	AN(*ptr);
	return (VFP_OK);
}

void
VFP_Extend(const struct vfp_ctx *vc, ssize_t sz, enum vfp_status flg)
{
	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);

	ObjExtend(vc->wrk, vc->oc, sz, flg == VFP_END ? 1 : 0);
}

/**********************************************************************
 */

void
VFP_Setup(struct vfp_ctx *vc, struct worker *wrk)
{

	INIT_OBJ(vc, VFP_CTX_MAGIC);
	VTAILQ_INIT(&vc->vfp);
	vc->wrk = wrk;
}

/**********************************************************************
 * Returns the number of bytes processed by the lowest VFP in the stack
 */

uint64_t
VFP_Close(struct vfp_ctx *vc)
{
	struct vfp_entry *vfe, *tmp;
	uint64_t rv = 0;

	VTAILQ_FOREACH_SAFE(vfe, &vc->vfp, list, tmp) {
		if (vfe->vfp->fini != NULL)
			vfe->vfp->fini(vc, vfe);
		rv = vfe->bytes_out;
		VSLb(vc->wrk->vsl, SLT_VfpAcct, "%s %ju %ju", vfe->vfp->name,
		    (uintmax_t)vfe->calls, (uintmax_t)rv);
		VTAILQ_REMOVE(&vc->vfp, vfe, list);
	}
	return (rv);
}

int
VFP_Open(VRT_CTX, struct vfp_ctx *vc)
{
	struct vfp_entry *vfe;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc->resp, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(vc->wrk, WORKER_MAGIC);
	AN(vc->wrk->vsl);

	VTAILQ_FOREACH_REVERSE(vfe, &vc->vfp, vfp_entry_s, list) {
		if (vfe->vfp->init == NULL)
			continue;
		if (DO_DEBUG(DBG_PROCESSORS))
			VSLb(vc->wrk->vsl, SLT_Debug, "VFP_Open(%s)",
			     vfe->vfp->name);
		vfe->closed = vfe->vfp->init(ctx, vc, vfe);
		if (vfe->closed != VFP_OK && vfe->closed != VFP_NULL) {
			(void)VFP_Error(vc, "Fetch filter %s failed to open",
			    vfe->vfp->name);
			(void)VFP_Close(vc);
			return (-1);
		}
	}

	return (0);
}

/**********************************************************************
 * Suck data up from lower levels.
 * Once a layer return non VFP_OK, clean it up and produce the same
 * return value for any subsequent calls.
 */

enum vfp_status
VFP_Suck(struct vfp_ctx *vc, void *p, ssize_t *lp)
{
	enum vfp_status vp;
	struct vfp_entry *vfe, *vfe_prev;
	const char *prev_name = "<storage>";
	ssize_t limit;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	AN(p);
	AN(lp);
	limit = *lp;
	vfe = vc->vfp_nxt;
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	vc->vfp_nxt = VTAILQ_NEXT(vfe, list);

	vfe_prev = VTAILQ_PREV(vfe, vfp_entry_s, list);
	if (vfe_prev != NULL)
		prev_name = vfe_prev->vfp->name;

	if (vfe->closed == VFP_NULL) {
		/* Layer asked to be bypassed when opened */
		vp = VFP_Suck(vc, p, lp);
		VFP_DEBUG(vc, "bypassing %s vp=%d", vfe->vfp->name, vp);
	} else if (vfe->closed == VFP_OK) {
		vp = vfe->vfp->pull(vc, vfe, p, lp);
		VFP_DEBUG(vc, "%s pulled %zdB/%zdB from %s vp=%d",
		    prev_name, *lp, limit, vfe->vfp->name, vp);
		if (vp != VFP_OK && vp != VFP_END && vp != VFP_ERROR)
			vp = VFP_Error(vc, "Fetch filter %s returned %d",
			    vfe->vfp->name, vp);
		else
			vfe->bytes_out += *lp;
		vfe->closed = vp;
		vfe->calls++;
	} else {
		/* Already closed filter */
		*lp = 0;
		vp = vfe->closed;
		VFP_DEBUG(vc, "ignoring %s vp=%d", vfe->vfp->name, vp);
	}
	vc->vfp_nxt = vfe;
	assert(vp != VFP_NULL);
	return (vp);
}

/*--------------------------------------------------------------------
 */

struct vfp_entry *
VFP_Push(struct vfp_ctx *vc, const struct vfp *vfp)
{
	struct vfp_entry *vfe;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc->resp, HTTP_MAGIC);

	vfe = WS_Alloc(vc->resp->ws, sizeof *vfe);
	if (vfe == NULL) {
		(void)VFP_Error(vc, "Workspace overflow");
		return (NULL);
	}

	INIT_OBJ(vfe, VFP_ENTRY_MAGIC);
	vfe->vfp = vfp;
	vfe->closed = VFP_OK;
	VTAILQ_INSERT_HEAD(&vc->vfp, vfe, list);
	vc->vfp_nxt = vfe;
	return (vfe);
}

/*--------------------------------------------------------------------
 * Debugging aids
 */

static void v_matchproto_(cli_func_t)
debug_fragfetch(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	(void)cli;
	fetchfrag = strtoul(av[2], NULL, 0);
}

static struct cli_proto debug_cmds[] = {
	{ CLICMD_DEBUG_FRAGFETCH,		"d", debug_fragfetch },
	{ NULL }
};

/*--------------------------------------------------------------------
 *
 */

void
VFP_Init(void)
{

	CLI_AddFuncs(debug_cmds);
}
