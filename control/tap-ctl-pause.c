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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "tap-ctl.h"

int tap_ctl_pause(const int id, const int minor, struct timeval *timeout)
{
    int err;
    tapdisk_message_t message;

    memset(&message, 0, sizeof(message));
    message.type = TAPDISK_MESSAGE_PAUSE;
    message.cookie = minor;

    err = tap_ctl_connect_send_and_receive(id, &message, timeout);
    if (err)
        return err;

    if (message.type == TAPDISK_MESSAGE_PAUSE_RSP)
        err = message.u.response.error;
    else {
        err = EINVAL;
        EPRINTF("got unexpected result '%s' from %d\n",
                tapdisk_message_name(message.type), id);
    }

    return err;
}
