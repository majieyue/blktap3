/*
 * Copyright (c) 2010, Citrix Systems, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of XenSource Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "blktap.h"
#include "tapdisk-vbd.h"
#include "tapdisk-blktap.h"
#include "tapdisk-server.h"
#include "linux-blktap.h"

#define BUG(_cond)       td_panic()
#define BUG_ON(_cond)    if (unlikely(_cond)) { td_panic(); }

#define DBG(_f, _a...)       tlog_syslog(TLOG_DBG, _f, ##_a)
#define INFO(_f, _a...)      tlog_syslog(TLOG_INFO, _f, ##_a)
#define ERR(_err, _f, _a...) tlog_error(_err, _f, ##_a)
#define WARN(_f, _a...)      tlog_syslog(TLOG_WARN, "WARNING: "_f "in %s:%d", \
					 ##_a, __func__, __LINE__)

#define __RD2(_x)  (((_x) & 0x00000002) ? 0x2                  : ((_x) & 0x1))
#define __RD4(_x)  (((_x) & 0x0000000c) ? __RD2((_x)>>2)<<2    : __RD2(_x))
#define __RD8(_x)  (((_x) & 0x000000f0) ? __RD4((_x)>>4)<<4    : __RD4(_x))
#define __RD16(_x) (((_x) & 0x0000ff00) ? __RD8((_x)>>8)<<8    : __RD8(_x))
#define __RD32(_x) (((_x) & 0xffff0000) ? __RD16((_x)>>16)<<16 : __RD16(_x))

#define BLKTAP_RD32(_n)      __RD32(_n)
#define BLKTAP_RING_SIZE     __BLKTAP_RING_SIZE(BLKTAP_PAGE_SIZE)
#define BLKTAP_PAGE_SIZE     sysconf(_SC_PAGE_SIZE)

#define BLKTAP_GET_RESPONSE(_tap, _idx) \
	(&(_tap)->sring->entry[(_idx) % BLKTAP_RING_SIZE].rsp)
#define BLKTAP_GET_REQUEST(_tap, _idx) \
	(&(_tap)->sring->entry[(_idx) % BLKTAP_RING_SIZE].req)

static void __tapdisk_blktap_close(td_blktap_t *);

struct td_blktap_req {
    td_vbd_request_t vreq;
    unsigned int id;
    char name[16];
    struct td_iovec iov[BLKTAP_SEGMENT_MAX];
};

void tapdisk_blktap_free_request(td_blktap_t * tap, td_blktap_req_t * req)
{
    BUG_ON(tap->n_reqs_free >= tap->n_reqs);
    tap->reqs_free[tap->n_reqs_free++] = req;
}

static void tapdisk_blktap_reqs_free(td_blktap_t * tap)
{
    if (tap->reqs) {
        free(tap->reqs);
        tap->reqs = NULL;
    }

    if (tap->reqs_free) {
        free(tap->reqs_free);
        tap->reqs_free = NULL;
    }
}

static int tapdisk_blktap_reqs_init(td_blktap_t * tap, int n_reqs)
{
    int i, err;

    tap->reqs = malloc(n_reqs * sizeof(td_blktap_req_t));
    if (!tap->reqs) {
        err = -errno;
        goto fail;
    }

    tap->reqs_free = malloc(n_reqs * sizeof(td_blktap_req_t *));
    if (!tap->reqs_free) {
        err = -errno;
        goto fail;
    }

    tap->n_reqs = n_reqs;
    tap->n_reqs_free = 0;

    for (i = 0; i < n_reqs; i++)
        tapdisk_blktap_free_request(tap, &tap->reqs[i]);

    return 0;

  fail:
    tapdisk_blktap_reqs_free(tap);
    return err;
}

int tapdisk_blktap_remove_device(td_blktap_t * tap)
{
    int err = 0;

    if (likely(tap->fd >= 0)) {
        err = ioctl(tap->fd, BLKTAP_IOCTL_REMOVE_DEVICE);
        if (err)
            err = -errno;
    }

    return err;
}

int
tapdisk_blktap_compat_create_device(td_blktap_t * tap,
                                    const struct blktap_device_info *bdi)
{
    struct blktap2_params params;
    int err;

    memset(&params, 0, sizeof(params));
    params.capacity = bdi->capacity;
    params.sector_size = bdi->sector_size;

    err = ioctl(tap->fd, BLKTAP_IOCTL_CREATE_DEVICE_COMPAT, &params);
    if (err) {
        err = -errno;
        return err;
    }

    if (bdi->flags || bdi->physical_sector_size != bdi->sector_size)
        WARN("fell back to compat ioctl(%d)",
             BLKTAP_IOCTL_CREATE_DEVICE_COMPAT);

    return 0;
}

#ifndef ENOIOCTLCMD
#define ENOIOCTLCMD 515
#endif

int
tapdisk_blktap_create_device(td_blktap_t * tap,
                             const td_disk_info_t * info, int rdonly)
{
    struct blktap_device_info bdi;
    unsigned long flags;
    int err;

    memset(&bdi, 0, sizeof(bdi));

    flags = 0;
    flags |= rdonly & TD_OPEN_RDONLY ? BLKTAP_DEVICE_RO : 0;

    bdi.capacity = info->size;
    bdi.sector_size = info->sector_size;
    bdi.physical_sector_size = info->sector_size;
    bdi.flags = flags;

    INFO("bdev: capacity=%llu sector_size=%u/%u flags=%#lx",
         bdi.capacity, bdi.sector_size, bdi.physical_sector_size,
         bdi.flags);

    err = ioctl(tap->fd, BLKTAP_IOCTL_CREATE_DEVICE, &bdi);
    if (!err)
        return 0;

    err = -errno;
    if (err == -ENOTTY || err == -ENOIOCTLCMD)
        err = tapdisk_blktap_compat_create_device(tap, &bdi);

    return err;
}

static void tapdisk_blktap_unmap(td_blktap_t * tap)
{
    if (tap->vma) {
        munmap(tap->vma, tap->vma_size);
        tap->vma = NULL;
    }
}

static void __tapdisk_blktap_close(td_blktap_t * tap)
{
    /*
     * NB. this can bail out at runtime. after munmap, blktap
     * already failed all pending block reqs. AIO on buffers will
     * -EFAULT. vreq completion just backs off once fd/vma are
     * gone, so we'll drain, then idle until close().
     */

    if (tap->event_id >= 0) {
        tapdisk_server_unregister_event(tap->event_id);
        tap->event_id = -1;
    }

    tapdisk_blktap_unmap(tap);

    if (tap->fd >= 0) {
        close(tap->fd);
        tap->fd = -1;
    }
}

void tapdisk_blktap_close(td_blktap_t * tap)
{
    __tapdisk_blktap_close(tap);
    tapdisk_blktap_reqs_free(tap);
    free(tap);
}

int
tapdisk_blktap_open(const char *devname, td_vbd_t * vbd,
                    td_blktap_t ** _tap)
{
    td_blktap_t *tap;
    int err;

    tap = malloc(sizeof(*tap));
    if (!tap) {
        err = -errno;
        goto fail;
    }

    memset(tap, 0, sizeof(*tap));
    tap->fd = -1;
    tap->event_id = -1;

    tap->vbd = vbd;

    err = tapdisk_blktap_reqs_init(tap, BLKTAP_RING_SIZE);
    if (err)
        goto fail;

    if (_tap)
        *_tap = tap;

    return 0;

  fail:
    if (tap)
        tapdisk_blktap_close(tap);

    return err;
}

void tapdisk_blktap_stats(td_blktap_t * tap, td_stats_t * st)
{
    tapdisk_stats_field(st, "minor", "d", tap->minor);

    tapdisk_stats_field(st, "reqs", "[");
    tapdisk_stats_val(st, "llu", tap->stats.reqs.in);
    tapdisk_stats_val(st, "llu", tap->stats.reqs.out);
    tapdisk_stats_leave(st, ']');

    tapdisk_stats_field(st, "kicks", "[");
    tapdisk_stats_val(st, "llu", tap->stats.kicks.in);
    tapdisk_stats_val(st, "llu", tap->stats.kicks.out);
    tapdisk_stats_leave(st, ']');
}
