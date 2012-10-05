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

#ifndef _XENIO_H
#define _XENIO_H

/* TODO ? */
#define _FILE_OFFSET_BITS 64

/* TODO some of these includes may be unnecessary */
#include <stdint.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <xen/event_channel.h>
#include <xen/io/blkif.h>

typedef struct xenio_ctx xenio_ctx_t;
typedef struct xenio_blkif xenio_blkif_t;
typedef struct xenio_blkif_req xenio_blkif_req_t;
struct tq_xenio_blkif;

/*
 * Create a guest I/O context.
 *
 * Comprises event notifications and granted memory mapping for one or a number
 * of data paths.
 *
 * @return a handle to the context, NULL on failure. Sets errno.
 */
xenio_ctx_t *xenio_open(void);

void xenio_close(xenio_ctx_t * ctx);

/**
 * Synchronous I/O multiplexing for guest notifications.
 *
 * @return a file descriptor suitable for polling.
 */
int xenio_event_fd(xenio_ctx_t * ctx);

/*
 * Block I/O.
 */
enum {
    XENIO_BLKIF_PROTO_NATIVE = 1,
    XENIO_BLKIF_PROTO_X86_32 = 2,
    XENIO_BLKIF_PROTO_X86_64 = 3,
};

/*
 * xenio_blkif_connect: Connect to a Xen block I/O ring in @ctx.
 *
 * @domid: Remote domain id
 * @grefs: 1 or a number or sring grant references.
 * @order: Ring order -- number of grefs, log-2.
 * @port:  Remote interdomain event channel port.
 * @proto: Ring compat for 32/64-bit-guests.
 * @data:  User token for xenio_pending_blkif.
 *
 * Returns a connection handle, or NULL on failure. Sets errno.
 */
xenio_blkif_t *xenio_blkif_connect(xenio_ctx_t * ctx, domid_t domid,
                                   const grant_ref_t * grefs, int order,
                                   evtchn_port_t port, int proto,
                                   void *data);

/**
 * Disconnects and destroy a blkif handle.
 *
 * @param blkif the XEN I/O block interface to disconnect and destroy.
 *
 * @note If blkif->ctx is non NULL, blkif is removed from the blkif->ctx
 * internal list.
 */
void xenio_blkif_disconnect(xenio_blkif_t * blkif);

/*
 * xenio_pending_blkif: Synchronous I/O multiplexing. Find all pending
 * blkifs on the given context, following guest event notification.
 */
xenio_blkif_t *xenio_pending_blkif(xenio_ctx_t * ctx, void **data);

/*
 * A single guest block request. Request structs are allocated by the
 * caller, but initialized by xenio.
 */

struct xenio_blkif_req {
    int op;
    uint64_t id;
    int status;

    off_t offset;
    struct xenio_blkif_seg {
        uint8_t first;
        uint8_t last;
    } segs[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    grant_ref_t gref[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    int n_segs;

    unsigned int pgoff;
    void *vma;
    struct iovec iov[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    int n_iov;
};

/*
 * Returns the size of the shared I/O ring after connecting, in
 * numbers of requests.
 */
int xenio_blkif_ring_size(xenio_blkif_t * blkif);

/*
 * Read up to @count request messages from the shared I/O ring.
 * Caller indicates @final to reenable notifications before it stops
 * reading.
 */
int xenio_blkif_get_requests(xenio_blkif_t * blkif,
                             blkif_request_t ** msgs, int count,
                             int final);

/*
 * Read a single request message.
 *
 * On success, returns 0 on leaves request content in
 * @req->{offset,segs,gref,n_segs}.
 *
 * Returns -errno of failure and sets req->status to BLKIF_RSP_ERROR.
 */
int xenio_blkif_parse_request(xenio_blkif_t * blkif, blkif_request_t * msg,
                              xenio_blkif_req_t * req);

/*
 * Trivial request mapping.
 *
 * Good for prototyping and applications not dealing with frame
 * pools. Or bound pools not prone to congestion.
 *
 * On success, returns 0 and leaves segment ranges in @req->vma and
 * @req->iovec. Sets and returns -errno on failure.
 */
int xenio_blkif_mmap_one(xenio_blkif_t * blkif, xenio_blkif_req_t * req);

int xenio_blkif_munmap_one(xenio_blkif_t * blkif, xenio_blkif_req_t * req);

/*
 * Batch request mapping.
 *
 * Good for applications with lots of guest I/O
 * on shared storage.
 */

/*
 * xenio_blkif_map_grants: Establish a grant mapping for a batch of
 * requests. If the blkif context was bound to a frame pool, mapped
 * requests will be backed with page structs. Despite this call
 * succeeding, the latter may happen only asynchronously.
 */
int64_t xenio_blkif_map_grants(xenio_blkif_t * blkif,
                               xenio_blkif_req_t ** reqs, int count);

/*
 * xenio_blkif_unmap_grants: Revoke a grant mapping. Prevents
 * re-mmapping, but doesn't affect existing VMAs, so may forego
 * munmapping through xenio_blkif_munmap_request.
 */
int xenio_blkif_unmap_grants(xenio_blkif_t * blkif, int64_t id);

/*
 * xenio_blkif_mmap_requests: Map a batch of requests with established
 * grant mapping(s) into task memory.
 *
 * Iff the grant device was bound to a frame pool, this may fail
 * transiently with EAGAIN. Poll xenio_grant_event_fd for
 * notifications.
 *
 * On success, returns 0 and leaves segment ranges in @req->vma and
 * @req->iovec. Sets and returns -errno on failure.
 */
int xenio_blkif_mmap_requests(xenio_blkif_t * blkif,
                              xenio_blkif_req_t ** reqs, int count);

/*
 * xenio_blkif_munmap_request: Unmap a previously mmapped @request.
 */
int xenio_blkif_munmap_request(xenio_blkif_t * blkif,
                               xenio_blkif_req_t * req);

/*
 * Write @count responses, with result codes according to
 */
void xenio_blkif_put_responses(xenio_blkif_t * blkif,
                               xenio_blkif_req_t ** reqs, int count,
                               int final);

#endif                          /* _XENIO_H */
