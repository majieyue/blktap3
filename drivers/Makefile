XEN_ROOT=$(CURDIR)/../../..
include $(XEN_ROOT)/tools/Rules.mk

BLKTAP_ROOT = ..

INST_DIR ?= /usr/bin

LIBVHDDIR = $(BLKTAP_ROOT)/vhd/lib

# FIXME tapdisk-client tapdisk-stream tapdisk-diff not in Citrix blktap2
IBIN = tapdisk td-util td-rated
QCOW_UTIL = img2qcow qcow-create qcow2raw
LOCK_UTIL = lock-util
INST_DIR = $(SBINDIR)

override CFLAGS += \
	-fno-strict-aliasing \
	-I$(BLKTAP_ROOT)/include \
	-I$(BLKTAP_ROOT)/drivers \
	-I$(BLKTAP_ROOT)/xenio \
	$(CFLAGS_libxenctrl) \
	-D_GNU_SOURCE \
	-DUSE_NFS_LOCKS

# TODO Why only in 64-bit?
ifeq ($(CONFIG_X86_64),y)
CFLAGS += -fPIC
endif

LDFLAGS += -L$(BLKTAP_ROOT)/xenio -lxenio -L$(XEN_ROOT)/tools/libxc -lxenctrl \
		   -luuid

VHDLIBS := -L$(LIBVHDDIR) -lvhd

# FIXME Do not exist in Citrix blktap2.
# REMUS-OBJS := block-remus.o
# REMUS-OBJS += hashtable.o
# REMUS-OBJS += hashtable_itr.o
# REMUS-OBJS += hashtable_utility.o

ifneq ($(CONFIG_SYSTEM_LIBAIO),y)
CFLAGS += -I $(LIBAIO_DIR)
LIBAIO_DIR = $(XEN_ROOT)/tools/libaio/src
tapdisk tapdisk-stream tapdisk-diff $(QCOW_UTIL): AIOLIBS := $(LIBAIO_DIR)/libaio.a 
tapdisk-client tapdisk-stream tapdisk-diff $(QCOW_UTIL): CFLAGS += -I$(LIBAIO_DIR)
else
td-util td-rated tapdisk tapdisk-stream tapdisk-diff $(QCOW_UTIL): AIOLIBS := -laio
endif

MEMSHRLIBS :=
# FIXME __fixme__?
ifeq ($(CONFIG_Linux), __fixme__)
MEMSHR_DIR = $(XEN_ROOT)/tools/memshr
CFLAGS += -DMEMSHR
CFLAGS += -I $(MEMSHR_DIR)
MEMSHRLIBS += -L$(XEN_ROOT)/tools/libxc -lxenctrl $(MEMSHR_DIR)/libmemshr.a
endif

ifeq ($(VHD_STATIC),y)
td-util: CFLAGS += -static
endif

TAP-OBJS-y := scheduler.o
TAP-OBJS-y += tapdisk-vbd.o
TAP-OBJS-y += tapdisk-control.o
TAP-OBJS-y += tapdisk-image.o
TAP-OBJS-y += tapdisk-driver.o
TAP-OBJS-y += tapdisk-disktype.o
TAP-OBJS-y += tapdisk-interface.o
TAP-OBJS-y += tapdisk-server.o
TAP-OBJS-y += tapdisk-queue.o
TAP-OBJS-y += tapdisk-filter.o
TAP-OBJS-y += tapdisk-log.o
TAP-OBJS-y += tapdisk-utils.o
TAP-OBJS-y += io-optimize.o
TAP-OBJS-y += lock.o
TAP-OBJS-y += tapdisk-blktap.o
TAP-OBJS-y += tapdisk-stats.o
TAP-OBJS-y += tapdisk-xenblkif.o
TAP-OBJS-y += tapdisk-storage.o
TAP-OBJS-y += tapdisk-loglimit.o
TAP-OBJS-y += tapdisk-logfile.o
TAP-OBJS-y += tapdisk-syslog.o
TAP-OBJS-y += $(PORTABLE-OBJS-y)

MISC-OBJS-y := atomicio.o

BLK-OBJS-y := block-aio.o
BLK-OBJS-y += block-ram.o
BLK-OBJS-y += block-cache.o
BLK-OBJS-y += block-vhd.o
# FIXME not in Citrix's blktap2
#BLK-OBJS-y += block-log.o
#BLK-OBJS-y += block-qcow.o
#BLK-OBJS-y += aes.o
#BLK-OBJS-y += md5.o
BLK-OBJS-y += $(PORTABLE-OBJS-y)
BLK-OBJS-y += $(REMUS-OBJS)
BLK-OBJS-y += block-vindex.o
BLK-OBJS-y += block-lcache.o
BLK-OBJS-y += block-llcache.o
BLK-OBJS-y += block-valve.o

# FIXME qcow-util not in Citrix blktap2
all: $(IBIN) lock-util

$(BLKTAP_ROOT)/xenio/libxenio.a:
	make -C $(BLKTAP_ROOT)/xenio libxenio.a

$(BLKTAP_ROOT)/vhd/lib/libvhd.a:
	make -C $(BLKTAP_ROOT)/vhd/lib libvhd.a

tapdisk: $(TAP-OBJS-y) $(BLK-OBJS-y) $(MISC-OBJS-y) tapdisk.o \
	$(BLKTAP_ROOT)/xenio/libxenio.a $(BLKTAP_ROOT)/vhd/lib/libvhd.a
	$(CC) -o $@ $^ $(LDFLAGS) -lrt -lz $(VHDLIBS) $(AIOLIBS) $(MEMSHRLIBS) -lm 

tapdisk-client: tapdisk-client.o
	$(CC) -o $@ $^ $(LDFLAGS) -lrt

tapdisk-stream tapdisk-diff: %: %.o $(TAP-OBJS-y) $(BLK-OBJS-y)
	$(CC) -o $@ $^ $(LDFLAGS) -lrt -lz $(VHDLIBS) $(AIOLIBS) $(MEMSHRLIBS) -lm

td-util: $(TAP-OBJS-y) $(BLK-OBJS-y) $(MISC-OBJS-y) td.o
	$(CC) -o $@ $^ $(LDFLAGS) $(VHDLIBS) $(AIOLIBS)

td-rated: $(TAP-OBJS-y) $(BLK-OBJS-y) $(MISC-OBJS-y) td-rated.o
	$(CC) -o $@ $^ $(LDFLAGS) $(VHDLIBS) $(AIOLIBS)

lock-util: lock.c
	$(CC) $(CFLAGS) -DUTIL -o lock-util lock.c $(LDFLAGS)

.PHONY: qcow-util
qcow-util: img2qcow qcow2raw qcow-create

img2qcow qcow2raw qcow-create: %: %.o $(TAP-OBJS-y) $(BLK-OBJS-y)
	$(CC) -o $@ $^ $(LDFLAGS) -lrt -lz $(VHDLIBS) $(AIOLIBS) $(MEMSHRLIBS) -lm

# FIXME img2qcow, qcow-create, qcow2raw not built so not installed
install: all
	$(INSTALL_DIR) -p $(DESTDIR)$(INST_DIR)
	$(INSTALL_PROG) $(IBIN) $(LOCK_UTIL) $(DESTDIR)$(INST_DIR)

clean:
	rm -rf .*.d *.o *~ xen TAGS $(IBIN) $(LIB) $(LOCK_UTIL) $(QCOW_UTIL)

.PHONY: clean install
