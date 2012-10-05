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

#include <stdio.h>
#include <string.h>

#include "tap-ctl.h"
#include "tap-ctl-xen.h"

int tap_ctl_connect_xenblkif(pid_t pid, int minor, domid_t domid,
                             int devid, const grant_ref_t * grefs,
                             int order, evtchn_port_t port, int proto,
                             const char *pool)
{
    tapdisk_message_t message;
    int i, err;

    memset(&message, 0, sizeof(message));
    message.type = TAPDISK_MESSAGE_XENBLKIF_CONNECT;
    message.cookie = minor;

    message.u.blkif.domid = domid;
    message.u.blkif.devid = devid;
    for (i = 0; i < 1 << order; i++)
        message.u.blkif.gref[i] = grefs[i];
    message.u.blkif.order = order;
    message.u.blkif.port = port;
    message.u.blkif.proto = proto;
    if (pool)
        strncpy(message.u.blkif.pool, pool, sizeof(message.u.blkif.pool));
    else
        message.u.blkif.pool[0] = 0;

    err = tap_ctl_connect_send_and_receive(pid, &message, NULL);
    if (err)
        return err;

    if (message.type == TAPDISK_MESSAGE_XENBLKIF_CONNECT_RSP)
        err = -message.u.response.error;
    else
        err = -EINVAL;

    return err;
}

int tap_ctl_disconnect_xenblkif(pid_t pid, int minor, domid_t domid,
                                int devid, struct timeval *timeout)
{
    tapdisk_message_t message;
    int err;

    memset(&message, 0, sizeof(message));
    message.type = TAPDISK_MESSAGE_XENBLKIF_DISCONNECT;
    message.cookie = minor;
    message.u.blkif.domid = domid;
    message.u.blkif.devid = devid;

    err = tap_ctl_connect_send_and_receive(pid, &message, timeout);
    if (message.type == TAPDISK_MESSAGE_XENBLKIF_CONNECT_RSP)
        err = -message.u.response.error;
    else
        err = -EINVAL;

    return err;
}
