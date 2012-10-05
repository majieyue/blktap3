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
#include "tap-ctl-info.h"

int tap_ctl_info(pid_t pid, int minor, unsigned long long *sectors,
                 unsigned int *sector_size, unsigned int *info)
{
    tapdisk_message_t message;
    int err;

    memset(&message, 0, sizeof(message));
    message.type = TAPDISK_MESSAGE_DISK_INFO;
    message.cookie = minor;

    err = tap_ctl_connect_send_and_receive(pid, &message, NULL);
    if (err)
        return err;

    if (message.type != TAPDISK_MESSAGE_DISK_INFO_RSP)
        return -EINVAL;

    *sectors = message.u.image.sectors;
    *sector_size = message.u.image.sector_size;
    *info = message.u.image.info;
    return err;
}
