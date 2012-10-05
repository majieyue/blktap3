XEN_ROOT = $(CURDIR)/../../..
include $(XEN_ROOT)/tools/Rules.mk

IBIN = lvm-util

override CFLAGS += \
	-I$(BLKTAP_ROOT)/include \
	-D_GNU_SOURCE

# FIXME Why only on 64-bit?
ifeq ($(CONFIG_X86_64),y)
CFLAGS += -fPIC
endif

LVM-OBJS := lvm-util.o

all: build

build: $(IBIN) $(LVM-OBJS)

# TODO install in /usr/sbin?
install: all
	$(INSTALL_DIR) -p $(DESTDIR)$(SBINDIR)
	$(INSTALL_PROG) $(IBIN) $(DESTDIR)$(SBINDIR)

$(IBIN): lvm-util.c
	$(CC) -DLVM_UTIL $(LDFLAGS) -o $@ lvm-util.c

clean:
	rm -rf *.o *.opic *~ $(DEPS) $(IBIN)

.PHONY: all build clean install

-include $(DEPS)
