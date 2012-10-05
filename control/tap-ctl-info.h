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

#ifndef __TAP_CTL_INFO_H__
#define __TAP_CTL_INFO_H__

int tap_ctl_info(pid_t pid, int minor, unsigned long long *sectors,
		unsigned int *sector_size, unsigned int *info);

#endif /* __TAP_CTL_INFO_H__ */
