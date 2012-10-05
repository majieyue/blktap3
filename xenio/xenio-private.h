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

#ifndef __XENIO_PRIVATE_H__
#define __XENIO_PRIVATE_H__

#include "blktap.h"
#include <xenctrl.h>

struct xenio_blkif;
TAILQ_HEAD(tqh_xenio_blkif, xenio_blkif);

/**
 * TODO XEN I/O context?
 */
struct xenio_ctx {
	/**
	 * TODO grant table handle?
	 */
	xc_gnttab				*xcg_handle;

	/**
	 * TODO event channel handle?
	 */
	xc_evtchn				*xce_handle;

	/**
	 * List of XEN I/O block interfaces.
	 */
	struct tqh_xenio_blkif	ifs;
};

#include <stdlib.h>
#include <syslog.h>

void xenio_log(int prio, const char *fmt, ...);
void (*xenio_vlog)(int prio, const char *fmt, va_list ap);

#define DBG(_fmt, _args...)  xenio_log(LOG_DEBUG, "%s:%d "_fmt, __FILE__, \
		__LINE__, ##_args)
#define INFO(_fmt, _args...) xenio_log(LOG_INFO, _fmt, ##_args)
#define WARN(_fmt, _args...) xenio_log(LOG_WARNING, "%s:%d "_fmt, __FILE__, \
		__LINE__, ##_args)

#endif /* __XENIO_PRIVATE_H__ */
