/*
 * Copyright (C) 2012      Citrix Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <xenctrl.h>

#include "blktap.h"

#include "xenio.h"
#include "xenio-private.h"
#include "blkif.h"

void (*xenio_vlog)(int prio, const char *fmt, va_list ap) = vsyslog;

__printf(2, 3) void xenio_log(int prio, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	xenio_vlog(prio, fmt, ap);
	va_end(ap);
}

/**
 * TODO XEN I/O block interface.
 *
 * TODO Belongs to td_xenblkif???
 *
 * TODO Each XEN I/O block interface is a member of a list that lives in
 * xenio_blkif.ctx->ifs?
 */
struct xenio_blkif {
	/**
	 * Pointer to XEN I/O context.
	 */
	xenio_ctx_t					*ctx;

	domid_t						rd;
	evtchn_port_t				rport;
	evtchn_port_t				port;
	int							proto;

	blkif_back_rings_t			rings;
	grant_ref_t					ring_ref[8];
	int							ring_n_pages;

	unsigned int				sector_size;

	/**
	 * TODO rename to 'entry'
	 */
	TAILQ_ENTRY(xenio_blkif)	ctx_entry;
	void                        *data;
};

#define xenio_for_each_blkif(_ctx, _blkif)	\
	TAILQ_FOREACH(_blkif, &ctx->ifs, ctx_entry)

#define xenio_ctx_find_blkif(_ctx, _blkif, _cond)	\
	do {											\
		int found = 0;								\
		xenio_for_each_blkif(_ctx, _blkif) {		\
			if (_cond) {							\
				found = 1;							\
				break;								\
			}										\
		}											\
		if (!found)									\
			_blkif = NULL;							\
	} while (0);

int xenio_blkif_parse_request(xenio_blkif_t *blkif, blkif_request_t *msg,
		xenio_blkif_req_t *req)
{
	int i, err = -EINVAL;

	memset(req, 0, sizeof(req));

	req->op     = msg->operation;
	req->n_segs = msg->nr_segments;
	req->id     = msg->id;
	req->offset = msg->sector_number << 9;

	if (req->n_segs < 0 ||
	    req->n_segs > BLKIF_MAX_SEGMENTS_PER_REQUEST)
		goto fail;

	for (i = 0; i < req->n_segs; i++) {
		struct xenio_blkif_seg *seg = &req->segs[i];
		const int spp = XC_PAGE_SIZE >> 9;

		req->gref[i]  = msg->seg[i].gref;
		seg->first    = msg->seg[i].first_sect;
		seg->last     = msg->seg[i].last_sect;

		if (seg->last < seg->first)
			goto fail;

		if (spp < seg->last)
			goto fail;
	}

	err = 0;

fail:
	return err;
}

static inline void xenio_blkif_vector_request(xenio_blkif_req_t *req)
{
	const struct xenio_blkif_seg *seg;
	struct iovec *iov;
	void *page, *next, *last;
	size_t size;
	int i;

	iov  = req->iov - 1;
	last = NULL;
	page = req->vma;

	for (i = 0; i < req->n_segs; i++) {
		seg = &req->segs[i];

		next = page + (seg->first << 9);
		size = (seg->last - seg->first + 1) << 9;

		if (next != last) {
			iov++;
			iov->iov_base = next;
			iov->iov_len  = size;
		} else
			iov->iov_len += size;

		last  = iov->iov_base + iov->iov_len;
		page += XC_PAGE_SIZE;
	}

	req->n_iov = iov - req->iov + 1;
}

int xenio_blkif_munmap_one(xenio_blkif_t *blkif, xenio_blkif_req_t *req)
{
	xenio_ctx_t *ctx = blkif->ctx;
	int err;

	err = xc_gnttab_munmap(ctx->xcg_handle, req->vma, req->n_segs);
	if (err)
		return -errno;

	req->vma = NULL;
	return 0;
}

int xenio_blkif_mmap_one(xenio_blkif_t *blkif, xenio_blkif_req_t *req)
{
	xenio_ctx_t *ctx = blkif->ctx;
	int prot, err;

	prot  = PROT_READ;
	prot |= req->op == BLKIF_OP_READ ? PROT_WRITE : 0;

	req->vma = xc_gnttab_map_domain_grant_refs(ctx->xcg_handle,
			req->n_segs,
			blkif->rd,
			req->gref,
			prot);

	if (!req->vma) {
		err = -errno;
		goto fail;
	}

	xenio_blkif_vector_request(req);

	return 0;

fail:
	xenio_blkif_munmap_one(blkif, req);
	return err;
}

static inline int xenio_blkif_notify(xenio_blkif_t *blkif)
{
	xenio_ctx_t *ctx = blkif->ctx;
	int err;

	err = xc_evtchn_notify(ctx->xce_handle, blkif->port);
	if (err < 0) {
		WARN("error notifying event channel: %s\n", strerror(-errno));
		return -errno;
	}

	return 0;
}

/**
 * Utility function that retrieves a request using @idx as the ring index,
 * copying it to the @dst in a hardware independent way. 
 *
 * @param blkif the XEN I/O block interface
 * @param dst address that receives the request
 * @param rc the index of the request in the ring
 */
static inline void xenio_blkif_get_request(xenio_blkif_t *blkif,
		blkif_request_t *dst, RING_IDX idx)
{
	blkif_back_rings_t *rings = &blkif->rings;

	switch (blkif->proto) {
	case XENIO_BLKIF_PROTO_NATIVE: {
		blkif_request_t *src;
		src = RING_GET_REQUEST(&rings->native, idx);
		memcpy(dst, src, sizeof(blkif_request_t));
		break;
	}

	case XENIO_BLKIF_PROTO_X86_32: {
		blkif_x86_32_request_t *src;
		src = RING_GET_REQUEST(&rings->x86_32, idx);
		blkif_get_x86_32_req(dst, src);
		break;
	}

	case XENIO_BLKIF_PROTO_X86_64: {
		blkif_x86_64_request_t *src;
		src = RING_GET_REQUEST(&rings->x86_64, idx);
		blkif_get_x86_64_req(dst, src);
		break;
	}

	default:
		/* TODO gracefully fail? */
		abort();
	}
}

/**
 * Retrieves at most @count requests from the ring, copying them to @reqs.
 *
 * @param blkif the XEN I/O block interface
 * @param reqs array of pointers where each element points to sufficient memory
 * space that receives each request
 * @param count retrieve at most that many requests
 *
 * @returns the number of retrieved requests
 */
static inline int __xenio_blkif_get_requests(xenio_blkif_t *blkif,
		blkif_request_t **reqs, unsigned int count)
{
	blkif_common_back_ring_t *ring = &blkif->rings.common;
	RING_IDX rp, rc;
	int n;

    if (0 == count)
        return 0;

	rp = ring->sring->req_prod;
	xen_rmb();

	n = 0;
	for (rc = ring->req_cons; rc != rp; rc++) {
		blkif_request_t *dst = reqs[n];

		if (n++ >= count)
			break;

		xenio_blkif_get_request(blkif, dst, rc);
	}

	ring->req_cons = rc;

	return n;
}

int xenio_blkif_get_requests(xenio_blkif_t *blkif, blkif_request_t **reqs,
		int count, int final)
{
	blkif_common_back_ring_t *ring = &blkif->rings.common;
	int n = 0, work;

	do {
		if (final)
			RING_FINAL_CHECK_FOR_REQUESTS(ring, work);
		else
			work = RING_HAS_UNCONSUMED_REQUESTS(ring);

		if (!work)
			break;

		if (n >= count)
			break;

		n += __xenio_blkif_get_requests(blkif, reqs + n, count - n);
	} while (1);

	return n;
}

static inline blkif_response_t *xenio_blkif_get_response(xenio_blkif_t *blkif,
		RING_IDX rp)
{
	blkif_back_rings_t *rings = &blkif->rings;
	blkif_response_t *p;

	switch (blkif->proto) {
	case XENIO_BLKIF_PROTO_NATIVE:
		p = (blkif_response_t*)RING_GET_RESPONSE(&rings->native, rp);
		break;
	case XENIO_BLKIF_PROTO_X86_32:
		p = (blkif_response_t*)RING_GET_RESPONSE(&rings->x86_32, rp);
		break;
	case XENIO_BLKIF_PROTO_X86_64:
		p = (blkif_response_t*)RING_GET_RESPONSE(&rings->x86_64, rp);
		break;
	default:
		/* TODO gracefully fail? */
		abort();
	}

	return p;
}

void xenio_blkif_put_responses(xenio_blkif_t *blkif, xenio_blkif_req_t **reqs,
        int count, int final)
{
	blkif_common_back_ring_t *ring = &blkif->rings.common;
	int n, notify, err = 0;
	RING_IDX rp;

	for (rp = ring->rsp_prod_pvt, n = 0; n < count; n++, rp++) {
		const xenio_blkif_req_t *req = reqs[n];
		blkif_response_t *msg;

		msg = xenio_blkif_get_response(blkif, rp);

		msg->id        = req->id;
		msg->operation = req->op;
		msg->status    = req->status;
	}

	ring->rsp_prod_pvt = rp;

	if (final) {
		RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(ring, notify);
		if (notify) {
			err = xenio_blkif_notify(blkif);
			if (err) {
				WARN("error notifying block interface: %s\n", strerror(err));
			}
		}
	}
}


static void xenio_blkif_ring_unmap(xenio_blkif_t *blkif)
{
	xenio_ctx_t *ctx = blkif->ctx;
	void *sring = blkif->rings.common.sring;

	if (sring) {
		xc_gnttab_munmap(ctx->xcg_handle,
				 sring, blkif->ring_n_pages);
		blkif->rings.common.sring = NULL;
	}
}

static inline int xenio_blkif_ring_map(xenio_blkif_t *blkif,
		const grant_ref_t *grefs, int order)
{
	xenio_ctx_t *ctx = blkif->ctx;
	void *sring;
	int i, err;
	size_t sz;

	blkif->ring_n_pages = 1 << order;

	if (blkif->ring_n_pages > ARRAY_SIZE(blkif->ring_ref))
		return -EINVAL;

	for (i = 0; i < blkif->ring_n_pages; i++)
		blkif->ring_ref[i] = grefs[i];

	sring = xc_gnttab_map_domain_grant_refs(ctx->xcg_handle,
			blkif->ring_n_pages, blkif->rd, blkif->ring_ref,
			PROT_READ|PROT_WRITE);
	if (!sring) {
		err = -errno;
		goto fail;
	}

	sz = XC_PAGE_SIZE << order;

	switch (blkif->proto) {
	case XENIO_BLKIF_PROTO_NATIVE: {
		blkif_sring_t *__sring = sring;
		BACK_RING_INIT(&blkif->rings.native, __sring, sz);
		break;
	}
	case XENIO_BLKIF_PROTO_X86_32: {
		blkif_x86_32_sring_t *__sring = sring;
		BACK_RING_INIT(&blkif->rings.x86_32, __sring, sz);
		break;
	}
	case XENIO_BLKIF_PROTO_X86_64: {
		blkif_x86_64_sring_t *__sring = sring;
		BACK_RING_INIT(&blkif->rings.x86_64, __sring, sz);
		break;
	}
	default:
		err = -EINVAL;
		goto fail;
	}

	return 0;

fail:
	xenio_blkif_ring_unmap(blkif);
	return err;
}

static void xenio_blkif_evt_unbind(xenio_blkif_t *blkif)
{
	xenio_ctx_t *ctx = blkif->ctx;

	if (blkif->port >= 0) {
		xc_evtchn_unbind(ctx->xce_handle, blkif->port);
		blkif->port = -1;
	}
}

static inline int xenio_blkif_evt_bind(xenio_blkif_t *blkif, int port)
{
	xenio_ctx_t *ctx = blkif->ctx;
	evtchn_port_or_error_t lport;
	int err;

	lport = xc_evtchn_bind_interdomain(ctx->xce_handle, blkif->rd, port);
	if (lport < 0) {
		err = -errno;
		goto fail;
	}

	blkif->port  = lport;
	blkif->rport = port;

	return 0;

fail:
	xenio_blkif_evt_unbind(blkif);
	return err;
}

void xenio_blkif_disconnect(xenio_blkif_t *blkif)
{
	if (blkif->ctx)
		TAILQ_REMOVE(&blkif->ctx->ifs, blkif, ctx_entry);

	xenio_blkif_evt_unbind(blkif);

	xenio_blkif_ring_unmap(blkif);

	free(blkif);
}

xenio_blkif_t *xenio_blkif_connect(xenio_ctx_t *ctx, domid_t domid,
		const grant_ref_t *grefs, int order, evtchn_port_t port, int proto,
		void *data)
{
	xenio_blkif_t *blkif = NULL;
	int err;

	switch (proto) {
	case XENIO_BLKIF_PROTO_NATIVE:
	case XENIO_BLKIF_PROTO_X86_32:
	case XENIO_BLKIF_PROTO_X86_64:
		break;
	default:
		err = -EPROTONOSUPPORT;
		goto fail;
	}

	blkif = calloc(1, sizeof(*blkif));
	if (!blkif) {
		err = -errno;
		goto fail;
	}

	blkif->ctx    = ctx;
	blkif->rd     = domid;
	blkif->data   = data;
	blkif->proto  = proto;

	err = xenio_blkif_ring_map(blkif, grefs, order);
	if (err)
		goto fail;

	err = xenio_blkif_evt_bind(blkif, port);
	if (err)
		goto fail;

	TAILQ_INSERT_TAIL(&ctx->ifs, blkif, ctx_entry);

	return blkif;

fail:
	if (blkif) {
		/* 
		 * must set to NULL otherwise xenio_blkif_disconnect will try to remove
		 * it from the list.
		 */
		blkif->ctx = NULL;

		xenio_blkif_disconnect(blkif);
	}

	errno = -err;

	return NULL;
}

int xenio_blkif_ring_size(xenio_blkif_t *blkif)
{
	switch (blkif->proto) {
	case XENIO_BLKIF_PROTO_NATIVE:
		return RING_SIZE(&blkif->rings.native);

	case XENIO_BLKIF_PROTO_X86_32:
		return RING_SIZE(&blkif->rings.x86_32);

	case XENIO_BLKIF_PROTO_X86_64:
		return RING_SIZE(&blkif->rings.x86_64);
	}

	/* TODO gracefully fail? */
	abort();
}

xenio_blkif_t* xenio_pending_blkif(xenio_ctx_t *ctx, void **data)
{
	evtchn_port_or_error_t port;
	xenio_blkif_t *blkif;

	port = xc_evtchn_pending(ctx->xce_handle);
	if (port < 0)
		return NULL;

	xenio_ctx_find_blkif(ctx, blkif, blkif->port == port);
	if (blkif) {
		xc_evtchn_unmask(ctx->xce_handle, port);
		*data = blkif->data;
	}

	return blkif;
}
