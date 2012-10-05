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

#ifndef __TAP_CTL_XEN_H__
#define __TAP_CTL_XEN_H__

#include <xen/xen.h>
#include <xen/grant_table.h>
#include <xen/event_channel.h>

int tap_ctl_connect_xenblkif(pid_t pid, int minor,
			     domid_t domid, int devid,
			     const grant_ref_t *grefs, int order,
			     evtchn_port_t port,
			     int proto,
			     const char *pool);

int tap_ctl_disconnect_xenblkif(pid_t pid, int minor,
				domid_t domid, int devid,
				struct timeval *timeout);

#endif /* __TAP_CTL_XEN_H__ */
