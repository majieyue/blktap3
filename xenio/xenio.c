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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <xenstore.h>
#include <xen/xen.h>
#include <xen/io/xenbus.h>

#include <xen/grant_table.h>
#include <xen/event_channel.h>

#include "blktap.h"
#include "tap-ctl.h"
#include "tap-ctl-xen.h"
#include "tap-ctl-info.h"
#include "xenio.h"
#include "xenio-private.h"

#define __scanf(_f, _a)         __attribute__((format (scanf, _f, _a)))

/* FIXME */
#define BUG()                   abort()
#define BUG_ON(_cond)           if (unlikely(_cond)) BUG()

#define ORDER_TO_PAGES(o)    (1 << (o))

typedef struct xenio_backend xenio_backend_t;
typedef struct xenio_device xenio_device_t;

struct xenio_backend_ops {
    int (*probe) (xenio_device_t *, domid_t, const char *);
    void (*remove) (xenio_device_t *);
    int (*frontend_changed) (xenio_device_t *, XenbusState);
};

TAILQ_HEAD(tq_xenio_device, xenio_device);

/**
 * TODO Rename, there's no blkback anymore.
 * 
 * Additional state for the actual virtual block device.
 */
typedef struct blkback_device {
    dev_t dev;

    /* TODO already in struct xenio_device */
    domid_t domid;
    int devid;

    grant_ref_t *gref;
    evtchn_port_t port;

    /**
	 * Descriptor of the tapdisk process serving this virtual block device.
	 */
    tap_list_t tap;

    /**
	 * Sector size.
	 */
    unsigned int sector_size;

    /**
	 * Number of sectors.
	 */
    unsigned long long sectors;

    /**
	 * TODO ???
	 */
    unsigned int info;
} blkback_device_t;

/**
 * TODO A block device in the guest.
 */
struct xenio_device {

    /**
	 * TODO ???
	 */
    char *name;

    /**
	 * Pointer to backend.
	 * TODO Isn't there only one backend?
	 */
    xenio_backend_t *backend;

    /**
	 * For xenio_device lists.
	 */
     TAILQ_ENTRY(xenio_device) backend_entry;

    /**
	 * The serial number of the device.
	 * FIXME What is it used for?
	 */
    long long serial;

    /**
	 * TODO The domain ID this virtual device belongs to.
	 */
    domid_t domid;
    char *frontend_path;
    char *frontend_state_path;

    blkback_device_t *bdev;
};

/*
 * TODO "xenio" is defined in the IDL, take it from there
 */
#define XENIO_BACKEND_NAME	"xenio"
#define XENIO_BACKEND_PATH	"backend/"XENIO_BACKEND_NAME
#define XENIO_BACKEND_TOKEN	"backend-"XENIO_BACKEND_NAME

/**
 * A backend, essentially the collection of all necessary handles and
 * descriptors.
 */
static struct xenio_backend {

    /**
	 * A handle to xenstore.
	 */
    struct xs_handle *xs;
    xs_transaction_t xst;

    /**
	 * A backend has many devices.
	 */
    struct tq_xenio_device devices;

    /**
	 * For allocating serials.
	 */
    long long serial;

    const struct xenio_backend_ops *ops;
    int max_ring_page_order;
} backend;

#define xenio_backend_for_each_device(_device, _next, _backend)	\
	TAILQ_FOREACH_SAFE(_device, &(_backend)->devices, backend_entry, _next)

static char *vmprintf(const char *fmt, va_list ap)
{
    char *s;
    int n;

    n = vasprintf(&s, fmt, ap);
    if (n < 0)
        s = NULL;

    return s;
}

__printf(1, 2)
static char *mprintf(const char *fmt, ...)
{
    va_list ap;
    char *s;

    va_start(ap, fmt);
    s = vmprintf(fmt, ap);
    va_end(ap);

    return s;
}

/**
 * Reads the specified xenstore path. The caller must free the returned buffer.
 */
/* TODO Why don't we return the data pointer? */
static char *xenio_xs_vread(struct xs_handle *xs, xs_transaction_t xst,
                            const char *fmt, va_list ap)
{
    char *path, *data, *s = NULL;
    unsigned int len;

    path = vmprintf(fmt, ap);
    data = xs_read(xs, xst, path, &len);
    DBG("XS read %s -> %s \n", path, data);
    free(path);

    if (data) {
        s = strndup(data, len);
        free(data);
    }

    return s;
}

// FIXME causes a warning
//__printf(3, 4)
static char *xenio_xs_read(struct xs_handle *xs, xs_transaction_t xst,
                           const char *fmt, ...)
{
    va_list ap;
    char *s;

    va_start(ap, fmt);
    s = xenio_xs_vread(xs, xst, fmt, ap);
    va_end(ap);

    return s;
}

/**
 * Retrieves the device name from xenstore,
 * i.e. backend/xenio/<domid>/<devname>/@path
 */
static char *xenio_device_read(xenio_device_t * device, const char *path)
{
    xenio_backend_t *backend = device->backend;

    return xenio_xs_read(backend->xs, backend->xst, "%s/%d/%s/%s",
                         XENIO_BACKEND_PATH, device->domid, device->name, path);
}

/**
 * TODO Only called by xenio_device_scanf, merge into it
 */
static inline int xenio_device_vscanf(xenio_device_t * device,
                                      const char *path, const char *fmt,
                                      va_list ap)
{
    char *s;
    int n;

    s = xenio_device_read(device, path);
    if (!s)
        return -1;

    DBG("%s <- %s\n", path, s);
    n = vsscanf(s, fmt, ap);
    free(s);

    return n;
}

/**
 * TODO Only called by xenio_device_check_serial, merge into it.
 */
__scanf(3, 4)
static int xenio_device_scanf(xenio_device_t * device,
                              const char *path, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = xenio_device_vscanf(device, path, fmt, ap);
    va_end(ap);

    return n;
}

static char *xenio_device_read_otherend(xenio_device_t * device,
                                        const char *path)
{
    xenio_backend_t *backend = device->backend;

    return xenio_xs_read(backend->xs, backend->xst, "%s/%s",
                         device->frontend_path, path);
}

static int xenio_device_vscanf_otherend(xenio_device_t * device,
                                        const char *path, const char *fmt,
                                        va_list ap)
{
    char *s;
    int n;

    s = xenio_device_read_otherend(device, path);
    if (!s)
        return -1;

    n = vsscanf(s, fmt, ap);
    free(s);

    return n;
}

__scanf(3, 4)
static int xenio_device_scanf_otherend(xenio_device_t * device,
                                       const char *path, const char *fmt,
                                       ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = xenio_device_vscanf_otherend(device, path, fmt, ap);
    va_end(ap);

    return n;
}

/**
 * TODO Only called by xenio_device_printf. 
 */
static inline int xenio_device_vprintf(xenio_device_t * device,
                                       const char *key, int mkread,
                                       const char *fmt, va_list ap)
{
    xenio_backend_t *backend = device->backend;
    char *path = NULL, *val = NULL;
    bool nerr;
    int err;

    path =
        mprintf("%s/%d/%s/%s", XENIO_BACKEND_PATH, device->domid, device->name,
                key);
    if (!path) {
        err = -errno;
        goto fail;
    }

    val = vmprintf(fmt, ap);
    if (!val) {
        err = -errno;
        goto fail;
    }

    DBG("%s -> %s\n", path, val);
    nerr = xs_write(backend->xs, backend->xst, path, val, strlen(val));
    if (!nerr) {
        err = -errno;
        goto fail;
    }

    if (mkread) {
        struct xs_permissions perms = {
            device->domid,
            XS_PERM_READ
        };

        nerr =
            xs_set_permissions(backend->xs, backend->xst, path, &perms, 1);
        if (!nerr) {
            err = -errno;
            goto fail;
        }
    }

    err = 0;

  fail:
    if (path)
        free(path);
    if (val)
        free(val);

    return err;
}

/**
 * Writes to xenstore backened/xenio/<domid>/<devname>/@key = @fmt. If @mkread
 * is non-zero TODO the xenstore key/value pair is read-only?
 */
__printf(4, 5)
static int xenio_device_printf(xenio_device_t * device,
                               const char *key, int mkread,
                               const char *fmt, ...)
{
    va_list ap;
    int err;

    va_start(ap, fmt);
    err = xenio_device_vprintf(device, key, mkread, fmt, ap);
    va_end(ap);

    return err;
}

/**
 * TODO Only called by xenio_backend_probe_device
 */
static inline long long xenio_device_check_serial(xenio_device_t * device)
{
    long long serial;
    int n, err;

    /* read backend/xenio/<domid>/<device name>/xenio-serial from xenstore */
    n = xenio_device_scanf(device, "xenio-serial", "%lld", &serial);
    if (n != 1) {
        err = -EEXIST;
        goto fail;
    }

    if (serial != device->serial) {
        err = -EXDEV;
        goto fail;
    }

    err = 0;
  fail:
    return err;
}

static void xenio_device_unwatch_frontend_state(xenio_device_t * device)
{
    xenio_backend_t *backend = device->backend;

    if (device->frontend_state_path)
        xs_unwatch(backend->xs,
                   device->frontend_state_path, "otherend-state");

    if (device->frontend_state_path) {
        free(device->frontend_state_path);
        device->frontend_state_path = NULL;
    }
}

/**
 * Watches the frontend path, using otherend-state as the token. I.e.
 * /local/domain/<domid>/device/vbd/<devname>/state
 *
 * TODO Only called by xenio_backend_create_device
 */
static inline int xenio_device_watch_frontend_state(xenio_device_t *
                                                    device)
{
    xenio_backend_t *backend = device->backend;
    bool nerr;
    int err;

    device->frontend_state_path =
        mprintf("%s/state", device->frontend_path);
    if (!device->frontend_state_path) {
        err = -errno;
        goto fail;
    }

    DBG("watching %s\n", device->frontend_state_path);

    nerr =
        xs_watch(backend->xs, device->frontend_state_path,
                 "otherend-state");
    if (!nerr) {
        err = -errno;
        goto fail;
    }

    return 0;

  fail:
    xenio_device_unwatch_frontend_state(device);
    return err;
}

/**
 *
 * TODO Only called by xenio_backend_handle_otherend_watch, merge into it.
 */
static int xenio_device_check_frontend_state(xenio_device_t * device)
{
    xenio_backend_t *backend = device->backend;
    int state, err;
    char *s, *end;

    s = xenio_xs_read(backend->xs, backend->xst,
                      device->frontend_state_path);
    if (!s) {
        err = -errno;
        goto fail;
    }

    state = strtol(s, &end, 0);
    if (*end != 0 || end == s) {
        err = -EINVAL;
        goto fail;
    }

    err = backend->ops->frontend_changed(device, state);

  fail:
    free(s);
    return err;
}

int xenio_device_switch_state(xenio_device_t * device, XenbusState state)
{
    return xenio_device_printf(device, "state", 0, "%u", state);
}

static void xenio_backend_destroy_device(xenio_backend_t * backend,
                                         xenio_device_t * device)
{
    TAILQ_REMOVE(&backend->devices, device, backend_entry);

    xenio_device_unwatch_frontend_state(device);

    if (device->frontend_path) {
        free(device->frontend_path);
        device->frontend_path = NULL;
    }

    if (device->name) {
        free(device->name);
        device->name = NULL;
    }

    free(device);
}

static void xenio_backend_remove_device(xenio_backend_t * backend,
                                        xenio_device_t * device)
{
    backend->ops->remove(device);
    xenio_backend_destroy_device(backend, device);
}


/**
 * Creates a device and adds it to the list of devices in the backend.
 * Initiates a xenstore watch to the blkfront state.
 *
 * Creating the device implies initializing the handle and retrieving all the
 * information of the tapdisk that serves this VBD.
 */
static inline int xenio_backend_create_device(xenio_backend_t * backend,
                                              int domid, const char *name)
{
    xenio_device_t *device;
    int err;

    DBG("creating device %d/%s\n", domid, name);

    device = calloc(1, sizeof(*device));
    if (!device) {
        WARN("error allocating memory\n");
        err = -errno;
        goto fail;
    }

    device->backend = backend;
    device->serial = backend->serial++;
    device->domid = domid;

    TAILQ_INSERT_TAIL(&backend->devices, device, backend_entry);

    device->name = strdup(name);
    if (!device->name) {
        err = -errno;
        goto fail;
    }

    /*
     * Get the frontend path in xenstore. We need this to talk to blkfront.
     */
    device->frontend_path = xenio_device_read(device, "frontend");
    DBG("frontend = '%s' (%d)\n", device->frontend_path, errno);
    if (!device->frontend_path) {
        err = -errno;
        goto fail;
    }

    /*
     * Write to xenstore xenio-serial and max-ring-page-order.
     */
    /* FIXME What's xenio-serial? */
    err = xenio_device_printf(device, "xenio-serial", 0, "%lld",
                              device->serial);
    if (err)
        goto fail;

    if (device->backend->max_ring_page_order)
        err = xenio_device_printf(device, "max-ring-page-order", 0, "%d",
                                  device->backend->max_ring_page_order);
    if (err)
        goto fail;

    /*
     * Get the tapdisk that is serving this virtual block device, along with
     * it's parameters.
     */
    err = backend->ops->probe(device, device->domid, name);
    if (err)
        goto fail;

    err = xenio_device_watch_frontend_state(device);
    if (err)
        goto fail;

    return 0;

  fail:
    if (device) {
        WARN("error creating device: domid=%d name=%s err=%d (%s)\n",
             device->domid, device->name, err, strerror(-err));
        xenio_backend_destroy_device(backend, device);
    }

    return err;
}

/**
 * TODO Only called by xenio_backend_device_exists, merge into it?
 */
__printf(3, 4)
static inline bool xenio_xs_exists(struct xs_handle *xs,
                            xs_transaction_t xst, const char *fmt, ...)
{
    va_list ap;
    char *s;

    va_start(ap, fmt);
    s = xenio_xs_vread(xs, xst, fmt, ap);
    va_end(ap);
    if (s)
        free(s);

    return s != NULL;
}

/**
 * Tells whether the device exists on the domain by looking at xenstore.
 *
 * TODO Only called by xenio_backend_probe_device, merge into it?
 */
static inline bool xenio_backend_device_exists(xenio_backend_t * backend,
                                               int domid, const char *name)
{
    /* i.e. backend/xenio/<domid>/<device name> */
    return xenio_xs_exists(backend->xs, backend->xst, "%s/%d/%s",
                           XENIO_BACKEND_PATH, domid, name);
}

/**
 * Iterates over all devices and returns the one for each the condition is
 * true.
 */
#define xenio_backend_find_device(_backend, _device, _cond)		\
do {															\
	xenio_device_t *__next;										\
	int found = 0;												\
	xenio_backend_for_each_device(_device, __next, _backend) {	\
		if (_cond) {											\
			found = 1;											\
			break;												\
		}														\
	}															\
	if (!found)													\
		_device = NULL;											\
} while (0)

/**
 * Creates or removes a device.
 *
 * TODO Documents under which conditions the device gets created/removed.
 * TODO Find out what that xenio-serial thing does.
 */
static int xenio_backend_probe_device(xenio_backend_t * backend, int domid,
                                      const char *name)
{
    bool exists, create, remove;
    xenio_device_t *device;
    int err;

    DBG("probe device domid=%d name=%s\n", domid, name);

    /*
     * Ask xenstore if the device _should_ exist.
     */
    exists = xenio_backend_device_exists(backend, domid, name);

    /*
     * Search the device list in the backend for this specific device.
     */
    xenio_backend_find_device(backend, device,
                              device->domid == domid &&
                              !strcmp(device->name, name));

    /*
	 * If xenstore says that the device exists but it's not in our device list,
     * we must create it. If it's the other way around, this is a removal. If
     * xenstore says that the device exists and it's in our device list, TODO
     * we must check serial.
     */
    remove = device && !exists;
    create = exists && !device;

    DBG("exists=%d device=%p remove=%d create=%d\n",
        exists, device, remove, create);

    if (device && exists) {
        /*
         * check the device serial, to sync with fast
         * remove/re-create cycles.
         */
        remove = create = ! !xenio_device_check_serial(device);

        if (!create && !remove) {
            DBG("neither create nor remove\n");
        }
    }

    if (remove)
        xenio_backend_remove_device(backend, device);

    if (create) {
        err = xenio_backend_create_device(backend, domid, name);
        if (err)
            goto fail;
    }

    err = 0;
  fail:
    return err;
}

/**
 * XXX ???
 */
static inline int xenio_backend_scan(xenio_backend_t * backend)
{
    xenio_device_t *device, *next;
    unsigned int i, j, n, m;
    char **dir;

    /*
     * scrap all nonexistent devices
     */

    xenio_backend_for_each_device(device, next, backend)
        xenio_backend_probe_device(backend, device->domid, device->name);

    /*
     * probe the new ones
     */

    dir = xs_directory(backend->xs, backend->xst, XENIO_BACKEND_PATH, &n);
    if (!dir)
        return 0;

    for (i = 0; i < n; i++) {
        char *path, **sub, *end;
        int domid;

        domid = strtoul(dir[i], &end, 0);
        if (*end != 0 || end == dir[i])
            continue;

        path = mprintf("%s/%d", XENIO_BACKEND_PATH, domid);
        assert(path != NULL);

        sub = xs_directory(backend->xs, backend->xst, path, &m);
        free(path);

        for (j = 0; j < m; j++)
            xenio_backend_probe_device(backend, domid, sub[j]);

        free(sub);
    }

    free(dir);
    return 0;
}

/**
 * Handles changes on blkfront xenstore path.
 *
 * TODO Only called by xenio_backend_read_watch
 */
static inline int xenio_backend_handle_otherend_watch(xenio_backend_t *
                                                      backend, char *path)
{
    xenio_device_t *device;
    int err = 0;

    /*
     * Find the device that has the same frontend state path.
     *
     * TODO It seems that there should definitely be such a device in our list,
     * otherwise this function would not have executed at all, since we would
     * not be waiting on that xenstore path.  The xenstore patch we wait for
     * is: /local/domain/<domid>/device/vbd/<devname>/state. In order to watch
     * this path, it means that we have received a a device create request, so
     * the device will be there
     */
    xenio_backend_find_device(backend, device,
                              !strcmp(device->frontend_state_path, path));
    if (device) {
        DBG("device: domid=%d name=%s\n", device->domid, device->name);
        err = xenio_device_check_frontend_state(device);
    } else {
        WARN("XXX no device found!\n");
        BUG();
    }

    return err;
}

/**
 * TODO Only called by xenio_backend_read_watch.
 */
static inline int xenio_backend_handle_backend_watch(xenio_backend_t *
                                                     backend, char *path)
{
    char *s, *end, *name;
    int domid;

    /* the path should be in the format backend/<name>/<domid>/<device name>,
     * i.e. backend/xenio/1/51712 */

    /* ignore the backend/xenio part */
    s = strtok(path, "/");
    assert(!strcmp(s, "backend"));

    s = strtok(NULL, "/");
    if (!s)
        goto scan;

    assert(!strcmp(s, XENIO_BACKEND_NAME));

    s = strtok(NULL, "/");
    if (!s)
        goto scan;

    /* get the domain ID */
    domid = strtoul(s, &end, 0);
    if (*end != 0 || end == s)
        return -EINVAL;

    /* get the device name */
    name = strtok(NULL, "/");
    if (!name)
        goto scan;

    /*
     * Create or remove the device.
     *
     * TODO Or check it's serial.
     */
    return xenio_backend_probe_device(backend, domid, name);

  scan:
    return xenio_backend_scan(backend);
}

/**
 * TODO watch backend/xenio
 */
static inline void xenio_backend_read_watch(xenio_backend_t * backend)
{
    char **watch, *path, *token;
    unsigned int n;
    int err, _abort;

    /* read the change */
    watch = xs_read_watch(backend->xs, &n);
    path = watch[XS_WATCH_PATH];
    token = watch[XS_WATCH_TOKEN];

    DBG("--\n");
    DBG("path=%s token=%s\n", path, token);

  again:
    backend->xst = xs_transaction_start(backend->xs);
    if (!backend->xst) {
        WARN("error starting transaction\n");
        goto fail;
    }

    /*
     * The initial path is backend/@name, i.e. backend/xenio.
     */
    switch (token[0]) {

        /*
         * TODO Is this blkfront?
         */
    case 'o':
        if (!strcmp(token, "otherend-state")) {
            err = xenio_backend_handle_otherend_watch(backend, path);
            break;
        }
        /* TODO gracefully fail? */
        BUG();

        /*
         * TODO verify the following:
         *
         * When blkfront (TODO or libxl?) request a new VBD, the path
         * '/<domid>/<devname>' is appended to 'backend/xenio'. In response,
         * xenio creates the VBD handle and initializes it (i.e. it finds which
         * tapdisk serves this VBD).)
         *
         * TODO The xenstore watch may trigger for a not yet discovered reason.
         * The result is xenio_backend_handle_backend_watch not doing anything
         * interesting, it only checks the 'xenio-serial'.
         */
    case 'b': /* token is backend-xenio */
        if (!strcmp(token, XENIO_BACKEND_TOKEN)) {
            err = xenio_backend_handle_backend_watch(backend, path);
            break;
        }
        /* TODO gracefully fail? */
        BUG();

    default:
        /* TODO gracefully fail? */
        BUG();
    }

    _abort = ! !err;
    if (_abort)
        DBG("aborting transaction: %s\n", strerror(-err));

    err = xs_transaction_end(backend->xs, backend->xst, _abort);
    backend->xst = 0;
    if (!err) {
        err = -errno;
        if (errno == EAGAIN) {
            DBG("xs_transaction_end failed, retrying...\n");
            goto again;
        }
        DBG("xs_transaction_end failed: %s\n", strerror(err));
    }

  fail:
    if (watch)
        free(watch);
    return;
}

/**
 * Retrieves the file descriptor of the xenstore watch. This fd can be polled
 * to detect changes on the watched xenstore path.
 */
int xenio_backend_fd(xenio_backend_t * backend)
{
    return xs_fileno(backend->xs);
}

static void xenio_backend_destroy(xenio_backend_t * backend)
{
    if (backend->xs) {
        xs_daemon_close(backend->xs);
        backend->xs = NULL;
    }

    free(backend);
}

/**
 * Initializes the backend descriptor. There is one backend per xenio process.
 * Also, it initiates a watch to xenstore on backned/<name>.
 *
 * TODO What sense does it make to use a @name different from xenio, since
 * libxl uses xenio?
 * TODO What's the use of the token? Aren't we the only ones to watch this
 * path? It would only make sense if we want to run multiple xenio daemons.
 */
static inline int xenio_backend_create(const struct xenio_backend_ops *ops,
		int max_ring_page_order, xenio_backend_t * backend)
{
    bool nerr;
    int err = -EINVAL;

    if (!ops) {
        WARN("no backend operations\n");
        goto fail;
    }

    backend->max_ring_page_order = max_ring_page_order;
    TAILQ_INIT(&backend->devices);
    backend->xst = XBT_NULL;

    backend->xs = xs_daemon_open();
    if (!backend->xs) {
        err = -EINVAL;
        goto fail;
    }

    /* set a watch on @path */
    nerr = xs_watch(backend->xs, XENIO_BACKEND_PATH, XENIO_BACKEND_TOKEN);
    if (!nerr) {
        err = -errno;
        goto fail;
    }

    backend->ops = ops;

    return 0;

  fail:
    if (backend)
        xenio_backend_destroy(backend);

    return -err;
}

/**
 * Retrieves the tapdisk designated to serve this device.
 *
 * TODO Which information does it exactly return?
 */
static inline int blkback_find_tapdisk(blkback_device_t * bdev)
{
    struct tqh_tap_list list;
    tap_list_t *tap;
    int err = 0;
    int found = 0;

    err = tap_ctl_list(&list);
    if (err) {
        WARN("error listing tapdisks: %s\n", strerror(err));
        return err;
    }

    if (!TAILQ_EMPTY(&list)) {
        tap_list_for_each_entry(tap, &list)
            if (tap->minor == minor(bdev->dev)) {
            found = 1;
            break;
        }
    } else {
        WARN("no tapdisks\n");
    }

    if (!found)
        return -ENOENT;

    memcpy(&bdev->tap, tap, sizeof(bdev->tap));

    return 0;
}

static int blkback_read_otherend_proto(xenio_device_t * xbdev)
{
    char *s;

    s = xenio_device_read_otherend(xbdev, "protocol");
    if (!s)
        return XENIO_BLKIF_PROTO_NATIVE;

    switch (s[0]) {
    case 'x':
        if (!strcmp(s, "x86_32-abi"))
            return XENIO_BLKIF_PROTO_X86_32;

        if (!strcmp(s, "x86_64-abi"))
            return XENIO_BLKIF_PROTO_X86_64;
    }

    free(s);
    return -EINVAL;
}

int blkback_connect_tap(xenio_device_t * xbdev)
{
    blkback_device_t *bdev = xbdev->bdev;
    evtchn_port_t port;
    grant_ref_t *gref = NULL;
    int n, proto, err = 0;
    int order;
    char *pool;
    char ring_ref[12];

    if (bdev->gref) {
        DBG("blkback already connected to tapdisk.\n");
        goto write_info;
    }

    n = xenio_device_scanf_otherend(xbdev, "ring-page-order", "%d",
                                    &order);
    if (n != 1)
        order = 0;

    gref = calloc(ORDER_TO_PAGES(order), sizeof(grant_ref_t));
    if (!gref) {
        DBG("Failed to allocate memory for grant refs.\n");
        err = ENOMEM;
        goto fail;
    }

    if (order) {
        int i;
        for (i = 0; i < (1 << order); i++) {
            sprintf(ring_ref, "ring-ref%d", i);
            n = xenio_device_scanf_otherend(xbdev, ring_ref, "%u",
                                            &gref[i]);
            if (n != 1) {
                DBG("Failed to read grant ref #%d.\n", i);
                err = ENOENT;
                goto fail;
            }
            DBG("%s = %d\n", ring_ref, gref[i]);
        }
    } else {
        n = xenio_device_scanf_otherend(xbdev, "ring-ref", "%u", &gref[0]);
        if (n != 1) {
            DBG("Failed to read grant ref.\n");
            err = ENOENT;
            goto fail;
        }

        DBG("ring-ref = %d\n", gref[0]);
    }

    n = xenio_device_scanf_otherend(xbdev, "event-channel", "%u", &port);
    if (n != 1) {
        DBG("Failed to read event channel.\n");
        err = ENOENT;
        goto fail;
    }

    proto = blkback_read_otherend_proto(xbdev);
    if (proto < 0) {
        DBG("Failed to read protocol.\n");
        err = ENOENT;
        goto fail;
    }

    pool = xenio_device_read(xbdev, "sm-data/frame-pool");

    DBG("connecting vbd-%d-%d (order %d, evt %d, proto %d, pool %s)"
        " to tapdisk %d minor %d\n",
        bdev->domid, bdev->devid,
        order, port, proto, pool, bdev->tap.pid, bdev->tap.minor);

    err = tap_ctl_connect_xenblkif(bdev->tap.pid,
                                   bdev->tap.minor,
                                   bdev->domid,
                                   bdev->devid,
                                   gref, order, port, proto, pool);
    DBG("err=%d errno=%d\n", err, errno);
    if (err)
        goto fail;

    bdev->gref = gref;
    gref = NULL;
    bdev->port = port;

  write_info:

    err = xenio_device_printf(xbdev, "sector-size", 1, "%u",
                              bdev->sector_size);
    if (err) {
        DBG("Failed to write sector-size.\n");
        goto fail;
    }

    err = xenio_device_printf(xbdev, "sectors", 1, "%llu", bdev->sectors);
    if (err) {
        DBG("Failed to write sectors.\n");
        goto fail;
    }

    err = xenio_device_printf(xbdev, "info", 1, "%u", bdev->info);
    if (err) {
        DBG("Failed to write info.\n");
        goto fail;
    }

    err = xenio_device_switch_state(xbdev, XenbusStateConnected);
    if (err) {
        DBG("Failed to switch state %d\n", err);
    }

  fail:
    if (err) {
        if (bdev->gref) {
            tap_ctl_disconnect_xenblkif(bdev->tap.pid, bdev->tap.minor,
                                        bdev->domid, bdev->devid, NULL);

            gref = bdev->gref;
            bdev->gref = NULL;
        }
    }

    if (gref)
        free(gref);

    return err;
}

int blkback_disconnect_tap(xenio_device_t * xbdev)
{
    blkback_device_t *bdev = xbdev->bdev;
    int err = 0;

    if (bdev->gref == NULL || bdev->port < 0)
        return err;

    DBG("disconnecting vbd-%d-%d from tapdisk %d minor %d\n",
        bdev->domid, bdev->devid, bdev->tap.pid, bdev->tap.minor);

    err = tap_ctl_disconnect_xenblkif(bdev->tap.pid, bdev->tap.minor,
                                      bdev->domid, bdev->devid, NULL);
    if (err && errno != -ESRCH)
        goto fail;

    free(bdev->gref);
    bdev->gref = NULL;
    bdev->port = -1;

    err = xenio_device_switch_state(xbdev, XenbusStateClosed);
  fail:
    return err;
}

/**
 * Retrieves the virtual block device parameters from the tapdisk.
 *
 * TODO aren't these supplied by blkback_find_tapdisk/tap_ctl_list?
 * TODO only called by blkback_probe.
 * TODO Very misleading function name
 */
static inline int blkback_probe_device(xenio_device_t * xbdev)
{
    blkback_device_t *bdev = xbdev->bdev;
    int err;
    unsigned int info;

    /* TODO info is unused */
    err = tap_ctl_info(bdev->tap.pid, bdev->tap.minor, &bdev->sectors,
                       &bdev->sector_size, &info);
    if (err)
        WARN("tap_ctl_info failed %d\n", err);
    else
        DBG("sectors=%llu sector-size=%d\n", bdev->sectors,
            bdev->sector_size);

    return err;
}

static void blkback_device_destroy(blkback_device_t * bdev)
{
    if (bdev->gref)
        free(bdev->gref);
    free(bdev);
}

/**
 * Initializes the virtual block device with information regarding the tapdisk
 * that is serving it.
 *
 * TODO The function name is very misleading.
 */
static int blkback_probe(xenio_device_t * xbdev, domid_t domid,
                         const char *name)
{
    blkback_device_t *bdev;
    int err;
    char *end;

    DBG("probe blkback %s-%d-%s\n", XENIO_BACKEND_NAME, xbdev->domid,
        xbdev->name);

    bdev = calloc(1, sizeof(*bdev));
    if (!bdev) {
        WARN("error allocationg memory\n");
        err = -errno;
        goto fail;
    }
    xbdev->bdev = bdev;

    bdev->domid = domid;
    bdev->devid = strtoul(name, &end, 0);
    if (*end != 0 || end == name) {
        err = -EINVAL;
        goto fail;
    }

    DBG("devid=%d\n", bdev->devid);
    err = blkback_find_tapdisk(bdev);
    if (err) {
        WARN("error looking for tapdisk: %s", strerror(err));
        goto fail;
    }

    blkback_probe_device(xbdev);
    DBG("got %s-%d-%d with tapdev %d/%d\n", XENIO_BACKEND_NAME,
        xbdev->domid, bdev->devid, bdev->tap.pid, bdev->tap.minor);

    return 0;

  fail:
    if (bdev)
        blkback_device_destroy(bdev);

    return err;
}

void blkback_remove(xenio_device_t * xbdev)
{
    blkback_device_t *bdev = xbdev->bdev;

    DBG("remove %s-%d-%s\n",
        XENIO_BACKEND_NAME, xbdev->domid, xbdev->name);

    blkback_device_destroy(bdev);
}

/**
 * TODO Only called by xenio_device_check_frontend_state.
 */
static inline int blkback_frontend_changed(xenio_device_t * xbdev,
                                           XenbusState state)
{
    int err = 0;

    DBG("frontend_changed %s-%d-%s state=%d\n",
        XENIO_BACKEND_NAME, xbdev->domid, xbdev->name, state);

    switch (state) {
    case XenbusStateUnknown:
        /* TODO wtf */
        break;

        /* 
         * Switch our state from Initialising (1) to InitWait (2). Absolutely
         * nothing else to do.
         */
    case XenbusStateInitialising:
        err = xenio_device_switch_state(xbdev, XenbusStateInitWait);
        break;

        /*
         * XXX 
         */
    case XenbusStateInitialised:
    case XenbusStateConnected:
        err = blkback_connect_tap(xbdev);
        break;

    case XenbusStateClosing:
        err = xenio_device_switch_state(xbdev, XenbusStateClosing);
        break;

    case XenbusStateClosed:
        err = blkback_disconnect_tap(xbdev);
        break;

    case XenbusStateReconfiguring:
    case XenbusStateReconfigured:
        /* wtf */
        break;

    case XenbusStateInitWait:
        /* fatal */
        break;
    }

    return err;
}

static struct xenio_backend_ops blkback_ops = {
    .probe = blkback_probe,
    .remove = blkback_remove,
    .frontend_changed = blkback_frontend_changed,
};

/**
 * Runs the backend.
 *
 * It watches xenstore (backend/xenio) for changes and when it detects one, it
 * creates or removes a VBD.
 */
static int blkback_run(xenio_backend_t * backend)
{
    int fd, err;

    /* get the fd of the xenstore path we're watching */
    fd = xenio_backend_fd(backend);

    do {
        fd_set rfds;
        int nfds;

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        /* poll the fd for changes in the xenstore path we're interested in */
        nfds = select(fd + 1, &rfds, NULL, NULL, NULL);
        if (nfds < 0) {
            perror("select");
            err = -errno;
            break;
        }

        if (FD_ISSET(fd, &rfds))
            xenio_backend_read_watch(backend);
    } while (1);

    return err;
}

static char *blkback_ident;

static void blkback_vlog_fprintf(int prio, const char *fmt, va_list ap)
{
    const char *strprio[] = {
        [LOG_DEBUG] = "DBG",
        [LOG_INFO] = "INF",
        [LOG_WARNING] = "WRN"
    };

    BUG_ON(prio < 0);
    BUG_ON(prio > sizeof(strprio) / sizeof(strprio[0]));
    BUG_ON(!strprio[prio]);

    fprintf(stderr, "%s[%s] ", blkback_ident, strprio[prio]);
    vfprintf(stderr, fmt, ap);
}

static void usage(FILE * stream, const char *prog)
{
    fprintf(stream,
            "usage: %s\n"
            "\t[-m|--max-ring-order <max ring page order>]\n"
            "\t[-D|--debug]\n"
			"\t[-h|--help]\n", prog);
}

int main(int argc, char **argv)
{
    const char *prog;
    int opt_debug, opt_max_ring_page_order;
    int err;

    prog = basename(argv[0]);

    opt_debug = 0;
    opt_max_ring_page_order = 0;

    do {
        const struct option longopts[] = {
            {"help", 0, NULL, 'h'},
            {"max-ring-order", 1, NULL, 'm'},
            {"debug", 0, NULL, 'D'},
        };
        int c;

        c = getopt_long(argc, argv, "hm:n:D", longopts, NULL);
        if (c < 0)
            break;

        switch (c) {
        case 'h':
            usage(stdout, prog);
            return 0;
        case 'm':
            opt_max_ring_page_order = atoi(optarg);
            if (opt_max_ring_page_order < 0)
                goto usage;
            break;
        case 'D':
            opt_debug = 1;
            break;
        case '?':
            goto usage;
        }
    } while (1);

    blkback_ident = XENIO_BACKEND_TOKEN;
    if (opt_debug)
        xenio_vlog = blkback_vlog_fprintf;
    else
        openlog(blkback_ident, 0, LOG_DAEMON);

    if (!opt_debug) {
        err = daemon(0, 0);
        if (err) {
            err = -errno;
            goto fail;
        }
    }

	if (!(err = xenio_backend_create( &blkback_ops, opt_max_ring_page_order,
					&backend))) {
        WARN("error creating blkback: %s\n", strerror(err));
        goto fail;
    }

    err = blkback_run(&backend);

    xenio_backend_destroy(&backend);

  fail:
    return err ? -err : 0;

  usage:
    usage(stderr, prog);
    return 1;
}
