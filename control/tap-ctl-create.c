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
#include <getopt.h>
#include <string.h>

#include "tap-ctl.h"
#include "blktap.h"

/* TODO devname not used */
int
tap_ctl_create(const char *params, int flags, int parent_minor,
               char *secondary)
{
    int err, id, minor = 0;

    id = tap_ctl_spawn();
    if (id < 0) {
        EPRINTF("error spawning tapdisk: %s", strerror(id));
        err = id;
        goto destroy;
    }

    err = tap_ctl_attach(id, minor);
    if (err) {
        EPRINTF("error attaching to tapdisk: %s", strerror(err));
        goto destroy;
    }

    err = tap_ctl_open(id, minor, params, flags, parent_minor, secondary);
    if (err) {
        EPRINTF("error opening tapdisk: %s", strerror(err));
        goto detach;
    }

    return 0;

  detach:
    tap_ctl_detach(id, minor);
  destroy:
    return err;
}
