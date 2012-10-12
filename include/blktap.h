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

#ifndef __BLKTAP_3_H__
#define __BLKTAP_3_H__

/* TODO remove from other files */
#include <xen-external/bsd-sys-queue.h>

#define BLKTAP3_SYSFS_DIR              "/sys/class/blktap3"
#define BLKTAP3_CONTROL_NAME           "blktap-control"
#define BLKTAP3_CONTROL_DIR            "/var/run/"BLKTAP3_CONTROL_NAME
#define BLKTAP3_CONTROL_SOCKET         "ctl"
#define BLKTAP3_DIRECTORY              "/dev/xen/blktap-3"
#define BLKTAP3_RING_DEVICE            BLKTAP3_DIRECTORY"/blktap"
#define BLKTAP3_IO_DEVICE              BLKTAP3_DIRECTORY"/tapdev"
#define BLKTAP3_ENOSPC_SIGNAL_FILE     "/var/run/tapdisk3-enospc"

/* TODO move elsewhere */
#define likely(_cond)           __builtin_expect(!!(_cond), 1)
#define unlikely(_cond)         __builtin_expect(!!(_cond), 0)

/* FIXME taken from list.h */
#define containerof(_ptr, _type, _memb) \
	((_type*)((void*)(_ptr) - offsetof(_type, _memb)))

/* FIXME */
#define __printf(a, b) __attribute__((format(printf, a, b)))

#define __scanf(_f, _a)         __attribute__((format (scanf, _f, _a)))

#define TAILQ_MOVE_HEAD(node, src, dst, entry)	\
	TAILQ_REMOVE(src, node, entry);				\
	TAILQ_INSERT_HEAD(dst, node, entry);

#define TAILQ_MOVE_TAIL(node, src, dst, entry)	\
	TAILQ_REMOVE(src, node, entry);				\
	TAILQ_INSERT_TAIL(dst, node, entry);

/*
 * TODO This is defined in xen/lib.h, use that instead of redefining it
 * here.
 */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif /* ARRAY_SIZE */

#endif                          /* __BLKTAP_3_H__ */
