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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/time.h>

#include "tap-ctl.h"

typedef int (*tap_ctl_func_t) (int, char **);

struct command {
	char                     *name;
	tap_ctl_func_t            func;
};

static void
tap_cli_list_usage(FILE *stream)
{
	fprintf(stream,
		"usage: list [-h] [-p pid] [-m minor] [-t type] [-f file]\n");
}

static void
tap_cli_list_row(tap_list_t *entry)
{
	char minor_str[10] = "-";
	char state_str[10] = "-";
	char pid_str[10]   = "-";

	if (entry->pid != -1)
		sprintf(pid_str, "%d", entry->pid);

	if (entry->minor != -1)
		sprintf(minor_str, "%d", entry->minor);

	if (entry->state != -1)
		sprintf(state_str, "%#x", entry->state);

	printf("%8s %4s %4s %10s %s\n",
	       pid_str, minor_str, state_str,
	       entry->type ? : "-", entry->path ? : "-");
}

static void
tap_cli_list_dict(tap_list_t *entry)
{
	int d = 0;

	if (entry->pid != -1) {
		if (d) putc(' ', stdout);
		d = printf("pid=%d", entry->pid);
	}

	if (entry->minor != -1) {
		if (d) putc(' ', stdout);
		d = printf("minor=%d", entry->minor);
	}

	if (entry->state != -1) {
		if (d) putc(' ', stdout);
		d = printf("state=%#x", entry->state);
	}

	if (entry->type && entry->path) {
		if (d) putc(' ', stdout);
		d = printf("args=%s:%s", entry->type, entry->path);
	}

	putc('\n', stdout);
}

int
tap_cli_list(int argc, char **argv)
{
	struct tqh_tap_list list = TAILQ_HEAD_INITIALIZER(list);
	int c, minor, tty, err;
	const char *type, *file;
	tap_list_t *entry;
	pid_t pid;

	pid   = -1;
	minor = -1;
	type  = NULL;
	file  = NULL;

	while ((c = getopt(argc, argv, "m:p:t:f:h")) != -1) {
		switch (c) {
		case 'm':
			minor = atoi(optarg);
			break;
		case 'p':
			pid = atoi(optarg);
			break;
		case 't':
			type = optarg;
			break;
		case 'f':
			file = optarg;
			break;
		case '?':
			goto usage;
		case 'h':
			tap_cli_list_usage(stdout);
			return 0;
		}
	}

	if (pid != -1)
		err = tap_ctl_list_pid(pid, &list);
	else
		err = tap_ctl_list(&list);
	if (err)
		return -err;

	tty = isatty(STDOUT_FILENO);

	tap_list_for_each_entry(entry, &list) {
		if (minor >= 0 && entry->minor != minor)
			continue;

		if (pid >= 0 && entry->pid != pid)
			continue;

		if (type && entry->type && strcmp(entry->type, type))
			continue;

		if (file && entry->path && strcmp(entry->path, file))
			continue;

		if (tty)
			tap_cli_list_row(entry);
		else
			tap_cli_list_dict(entry);
	}

	tap_ctl_list_free(&list);

	return 0;

usage:
	tap_cli_list_usage(stderr);
	return EINVAL;
}

static void
tap_cli_create_usage(FILE *stream)
{
	fprintf(stream, "usage: create <-a args> [-R readonly] "
		"[-e <minor> stack on existing tapdisk for the parent chain] "
		"[-r turn on read caching into leaf node] [-2 <path> "
		"use secondary image (in mirror mode if no -s)] [-s "
		"fail over to the secondary image on ENOSPC]\n");
}

static int
tap_cli_create(int argc, char **argv)
{
	int c, flags, prt_minor;
	char *args, *secondary;

	args      = NULL;
	secondary = NULL;
	prt_minor = -1;
	flags     = 0;

	optind = 0;
	while ((c = getopt(argc, argv, "a:R:e:r2:sh")) != -1) {
		switch (c) {
		case 'a':
			args = optarg;
			break;
		case 'R':
			flags |= TAPDISK_MESSAGE_FLAG_RDONLY;
			break;
		case 'r':
			flags |= TAPDISK_MESSAGE_FLAG_ADD_LCACHE;
			break;
		case 'e':
			flags |= TAPDISK_MESSAGE_FLAG_REUSE_PRT;
			prt_minor = atoi(optarg);
			break;
		case '2':
			flags |= TAPDISK_MESSAGE_FLAG_SECONDARY;
			secondary = optarg;
			break;
		case 's':
			flags |= TAPDISK_MESSAGE_FLAG_STANDBY;
			break;
		case '?':
			goto usage;
		case 'h':
			tap_cli_create_usage(stdout);
			return 0;
		}
	}

	if (!args)
		goto usage;

	return tap_ctl_create(args, flags, prt_minor, secondary);

usage:
	tap_cli_create_usage(stderr);
	return EINVAL;
}

static void
tap_cli_destroy_usage(FILE *stream)
{
	fprintf(stream, "usage: destroy <-p pid> <-m minor>\n");
}

static struct timeval*
tap_cli_timeout(const char *optarg)
{
	static struct timeval tv;
	struct timeval now;

	tv.tv_sec  = atoi(optarg);
	tv.tv_usec = 0;

	gettimeofday(&now, NULL);
	timeradd(&tv, &now, &tv);

	return &tv;
}

static int
tap_cli_destroy(int argc, char **argv)
{
	int c, pid, minor;
	struct timeval *timeout;

	pid     = -1;
	minor   = -1;
	timeout = NULL;

	optind = 0;
	while ((c = getopt(argc, argv, "p:m:t:h")) != -1) {
		switch (c) {
		case 'p':
			pid = atoi(optarg);
			break;
		case 'm':
			minor = atoi(optarg);
			break;
		case 't':
			timeout = tap_cli_timeout(optarg);
			if (!timeout)
				goto usage;
			break;
		case '?':
			goto usage;
		case 'h':
			tap_cli_destroy_usage(stdout);
			return 0;
		}
	}

	if (pid == -1 || minor == -1)
		goto usage;

	return tap_ctl_destroy(pid, minor, 0, timeout);

usage:
	tap_cli_destroy_usage(stderr);
	return EINVAL;
}

static void
tap_cli_spawn_usage(FILE *stream)
{
	fprintf(stream, "usage: spawn\n");
}

static int
tap_cli_spawn(int argc, char **argv)
{
	int c, tty;
	pid_t pid;

	optind = 0;
	while ((c = getopt(argc, argv, "h")) != -1) {
		switch (c) {
		case '?':
			goto usage;
		case 'h':
			tap_cli_spawn_usage(stdout);
			return 0;
		}
	}

	pid = tap_ctl_spawn();
	if (pid < 0)
		return pid;

	tty = isatty(STDOUT_FILENO);
	if (tty)
		printf("tapdisk spawned with pid %d\n", pid);
	else
		printf("%d\n", pid);

	return 0;

usage:
	tap_cli_spawn_usage(stderr);
	return EINVAL;
}

static void
tap_cli_attach_usage(FILE *stream)
{
	fprintf(stream, "usage: attach <-p pid> <-m minor>\n");
}

static int
tap_cli_attach(int argc, char **argv)
{
	int c, pid, minor;

	pid   = -1;
	minor = -1;

	optind = 0;
	while ((c = getopt(argc, argv, "p:m:h")) != -1) {
		switch (c) {
		case 'p':
			pid = atoi(optarg);
			break;
		case 'm':
			minor = atoi(optarg);
			break;
		case '?':
			goto usage;
		case 'h':
			tap_cli_attach_usage(stderr);
			return 0;
		}
	}

	if (pid == -1 || minor == -1)
		goto usage;

	return tap_ctl_attach(pid, minor);

usage:
	tap_cli_attach_usage(stderr);
	return EINVAL;
}

static void
tap_cli_detach_usage(FILE *stream)
{
	fprintf(stream, "usage: detach <-p pid> <-m minor>\n");
}

static int
tap_cli_detach(int argc, char **argv)
{
	int c, pid, minor;

	pid   = -1;
	minor = -1;

	optind = 0;
	while ((c = getopt(argc, argv, "p:m:h")) != -1) {
		switch (c) {
		case 'p':
			pid = atoi(optarg);
			break;
		case 'm':
			minor = atoi(optarg);
			break;
		case '?':
			goto usage;
		case 'h':
			tap_cli_detach_usage(stdout);
			return 0;
		}
	}

	if (pid == -1 || minor == -1)
		goto usage;

	return tap_ctl_detach(pid, minor);

usage:
	tap_cli_detach_usage(stderr);
	return EINVAL;
}

static void
tap_cli_close_usage(FILE *stream)
{
	fprintf(stream, "usage: close <-p pid> <-m minor> [-f force]\n");
}

static int
tap_cli_close(int argc, char **argv)
{
	int c, pid, minor, force;
	struct timeval *timeout;

	pid     = -1;
	minor   = -1;
	force   = 0;
	timeout = NULL;

	optind = 0;
	while ((c = getopt(argc, argv, "p:m:ft:h")) != -1) {
		switch (c) {
		case 'p':
			pid = atoi(optarg);
			break;
		case 'm':
			minor = atoi(optarg);
			break;
		case 'f':
			force = -1;
			break;
		case 't':
			timeout = tap_cli_timeout(optarg);
			if (!timeout)
				goto usage;
			break;
		case '?':
			goto usage;
		case 'h':
			tap_cli_close_usage(stdout);
			return 0;
		}
	}

	if (pid == -1 || minor == -1)
		goto usage;

	return tap_ctl_close(pid, minor, force, timeout);

usage:
	tap_cli_close_usage(stderr);
	return EINVAL;
}

static void
tap_cli_pause_usage(FILE *stream)
{
	fprintf(stream, "usage: pause <-p pid> <-m minor>\n");
}

static int
tap_cli_pause(int argc, char **argv)
{
	int c, pid, minor;
	struct timeval *timeout;

	pid     = -1;
	minor   = -1;
	timeout = NULL;

	optind = 0;
	while ((c = getopt(argc, argv, "p:m:t:h")) != -1) {
		switch (c) {
		case 'p':
			pid = atoi(optarg);
			break;
		case 'm':
			minor = atoi(optarg);
			break;
		case 't':
			timeout = tap_cli_timeout(optarg);
			if (!timeout)
				goto usage;
		case '?':
			goto usage;
		case 'h':
			tap_cli_pause_usage(stdout);
			return 0;
		}
	}

	if (pid == -1 || minor == -1)
		goto usage;

	return tap_ctl_pause(pid, minor, timeout);

usage:
	tap_cli_pause_usage(stderr);
	return EINVAL;
}

static void
tap_cli_unpause_usage(FILE *stream)
{
	fprintf(stream, "usage: unpause <-p pid> <-m minor> [-a args]\n");
}

int
tap_cli_unpause(int argc, char **argv)
{
	const char *args;
	int c, pid, minor;

	pid   = -1;
	minor = -1;
	args  = NULL;

	optind = 0;
	while ((c = getopt(argc, argv, "p:m:a:h")) != -1) {
		switch (c) {
		case 'p':
			pid = atoi(optarg);
			break;
		case 'm':
			minor = atoi(optarg);
			break;
		case 'a':
			args = optarg;
			break;
		case '?':
			goto usage;
		case 'h':
			tap_cli_unpause_usage(stdout);
			return 0;
		}
	}

	if (pid == -1 || minor == -1)
		goto usage;

	return tap_ctl_unpause(pid, minor, args);

usage:
	tap_cli_unpause_usage(stderr);
	return EINVAL;
}

static void
tap_cli_major_usage(FILE *stream)
{
	fprintf(stream, "usage: major [-h]\n");
}

static void
tap_cli_open_usage(FILE *stream)
{
	fprintf(stream, "usage: open <-p pid> <-m minor> <-a args> [-R readonly] "
		"[-e <minor> stack on existing tapdisk for the parent chain] "
		"[-r turn on read caching into leaf node] [-2 <path> "
		"use secondary image (in mirror mode if no -s)] [-s "
		"fail over to the secondary image on ENOSPC]\n");
}

static int
tap_cli_open(int argc, char **argv)
{
	const char *args, *secondary;
	int c, pid, minor, flags, prt_minor;

	flags     = 0;
	pid       = -1;
	minor     = -1;
	prt_minor = -1;
	args      = NULL;
	secondary = NULL;

	optind = 0;
	while ((c = getopt(argc, argv, "a:Rm:p:e:r2:sh")) != -1) {
		switch (c) {
		case 'p':
			pid = atoi(optarg);
			break;
		case 'm':
			minor = atoi(optarg);
			break;
		case 'a':
			args = optarg;
			break;
		case 'R':
			flags |= TAPDISK_MESSAGE_FLAG_RDONLY;
			break;
		case 'r':
			flags |= TAPDISK_MESSAGE_FLAG_ADD_LCACHE;
			break;
		case 'e':
			flags |= TAPDISK_MESSAGE_FLAG_REUSE_PRT;
			prt_minor = atoi(optarg);
			break;
		case '2':
			flags |= TAPDISK_MESSAGE_FLAG_SECONDARY;
			secondary = optarg;
			break;
		case 's':
			flags |= TAPDISK_MESSAGE_FLAG_STANDBY;
			break;
		case '?':
			goto usage;
		case 'h':
			tap_cli_open_usage(stdout);
			return 0;
		}
	}

	if (pid == -1 || minor == -1 || !args)
		goto usage;

	return tap_ctl_open(pid, minor, args, flags, prt_minor, secondary);

usage:
	tap_cli_open_usage(stderr);
	return EINVAL;
}

static void
tap_cli_stats_usage(FILE *stream)
{
	fprintf(stream, "usage: stats <-p pid> <-m minor>\n");
}

static int
tap_cli_stats(int argc, char **argv)
{
	pid_t pid;
	int c, minor, err;

	pid     = -1;
	minor   = -1;

	optind = 0;
	while ((c = getopt(argc, argv, "p:m:h")) != -1) {
		switch (c) {
		case 'p':
			pid = atoi(optarg);
			break;
		case 'm':
			minor = atoi(optarg);
			break;
		case '?':
			goto usage;
		case 'h':
			tap_cli_stats_usage(stdout);
			return 0;
		}
	}

	if (pid == -1 || minor == -1)
		goto usage;

	err = tap_ctl_stats_fwrite(pid, minor, stdout);
	if (err)
		return err;

	fprintf(stdout, "\n");

	return 0;

usage:
	tap_cli_stats_usage(stderr);
	return EINVAL;
}

struct command commands[] = {
	{ .name = "list",         .func = tap_cli_list          },
	{ .name = "create",       .func = tap_cli_create        },
	{ .name = "destroy",      .func = tap_cli_destroy       },
	{ .name = "spawn",        .func = tap_cli_spawn         },
	{ .name = "attach",       .func = tap_cli_attach        },
	{ .name = "detach",       .func = tap_cli_detach        },
	{ .name = "open",         .func = tap_cli_open          },
	{ .name = "close",        .func = tap_cli_close         },
	{ .name = "pause",        .func = tap_cli_pause         },
	{ .name = "unpause",      .func = tap_cli_unpause       },
	{ .name = "stats",        .func = tap_cli_stats         },
};

#define print_commands()					\
	do {							\
		int i, n;					\
		n = sizeof(commands) / sizeof(struct command);	\
		printf("COMMAND := { ");			\
		printf("%s", commands[0].name);			\
		for (i = 1; i < n; i++)				\
			printf(" | %s", commands[i].name);	\
		printf(" }\n");					\
	} while (0)

void
help(void)
{
	printf("usage: tap-ctl COMMAND [OPTIONS]\n");
	print_commands();
	exit(0);
}

struct command *
get_command(char *command)
{
	int i, n;

	if (strnlen(command, 25) >= 25)
		return NULL;

	n = sizeof(commands) / sizeof (struct command);

	for (i = 0; i < n; i++)
		if (!strcmp(command, commands[i].name))
			return &commands[i];

	return NULL;
}

int
main(int argc, char *argv[])
{
	char **cargv;
	struct command *cmd;
	int cargc, i, cnt, ret;

#ifdef CORE_DUMP
	#include <sys/resource.h>
	struct rlimit rlim;
	rlim.rlim_cur = RLIM_INFINITY;
	rlim.rlim_max = RLIM_INFINITY;
	if (setrlimit(RLIMIT_CORE, &rlim) < 0)
		PERROR("setrlimit failed");
#endif

	signal(SIGPIPE, SIG_IGN);

	ret = 0;

	if (argc < 2)
		help();

	cargc = argc - 1;
	cmd   = get_command(argv[1]);
	if (!cmd) {
		EPRINTF("invalid COMMAND %s", argv[1]);
		help();
	}

	cargv = malloc(sizeof(char *) * cargc);
	if (!cargv)
		exit(ENOMEM);

	cnt      = 1;
	cargv[0] = cmd->name;
	for (i = 1; i < cargc; i++) {
		char *arg = argv[i + (argc - cargc)];

		if (!strcmp(arg, "--debug")) {
			tap_ctl_debug = 1;
			continue;
		}

		cargv[cnt++] = arg;
	}

	ret = cmd->func(cnt, cargv);

	free(cargv);

	return (ret >= 0 ? ret : -ret);
}
