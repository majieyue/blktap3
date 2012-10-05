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
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "tap-ctl.h"
#include "blktap.h"

int
tap_ctl_destroy(const int id, const int minor,
		int force, struct timeval *timeout)
{
	int err;

	err = tap_ctl_close(id, minor, 0, timeout);
	if (err)
		return err;

	err = tap_ctl_detach(id, minor);
	if (err)
		return err;

	return 0;
}
