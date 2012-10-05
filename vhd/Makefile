XEN_ROOT=$(CURDIR)/../../..
include $(XEN_ROOT)/tools/Rules.mk

BLKTAP_ROOT := ..

INST_DIR ?= /usr/bin

SUBDIRS-y :=
SUBDIRS-y += lib

IBIN = vhd-index vhd-update vhd-util

override CFLAGS += \
	-I../include \
	-D_GNU_SOURCE \
	$(CFLAGS_xeninclude)

# TODO Why only in 64-bit?
ifeq ($(CONFIG_X86_64),y)
CFLAGS += -fPIC
endif

ifeq ($(VHD_STATIC),y)
CFLAGS += -static
endif

LIBS := -Llib -lvhd

all: subdirs-all build

build: $(IBIN)

LIBS_DEPENDS := lib/libvhd.so lib/libvhd.a
$(LIBS_DEPENDS): subdirs-all

vhd-index: vhd-index.o $(LIBS_DEPENDS)
	$(CC) $(LDFLAGS) -o vhd-index vhd-index.o $(LIBS)

vhd-update: vhd-update.o $(LIBS_DEPENDS)
	$(CC) $(LDFLAGS) -o vhd-update vhd-update.o $(LIBS)

vhd-util: vhd-util.o $(LIBS_DEPENDS)
	$(CC) $(LDFLAGS) -o vhd-util vhd-util.o $(LIBS)

install: all
	$(MAKE) subdirs-install
	$(INSTALL_DIR) -p $(DESTDIR)$(INST_DIR)
	$(INSTALL_PROG) $(IBIN) $(DESTDIR)$(INST_DIR)

clean: subdirs-clean
	rm -rf *.o *~ $(DEPS) $(IBIN)

.PHONY: all build clean install

# TODO Is this necessary?
-include $(DEPS)
