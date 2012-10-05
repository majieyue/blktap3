XEN_ROOT := $(CURDIR)/../../../
include $(XEN_ROOT)/tools/Rules.mk

BLKTAP_ROOT := ..

INST_DIR ?= /usr/bin

IBIN = xenio
LIB = lib$(IBIN).a

override CFLAGS += \
	-I$(BLKTAP_ROOT)/include \
	-I$(BLKTAP_ROOT)/control \
	-I$(XEN_ROOT)/tools/libxc \
	-I$(XEN_ROOT)/tools/xenstore \
	-D_GNU_SOURCE \
	$(CFLAGS_xeninclude)

LDFLAGS += -L$(XEN_ROOT)/tools/libxc -lxenctrl
LDFLAGS += -L$(XEN_ROOT)/tools/xenstore -lxenstore
LDFLAGS += -L../control -lblktapctl

XENIO-OBJS := xenio-blkif.o
XENIO-OBJS += xenio-ctx.o

all: $(IBIN) $(LIB)

$(BLKTAP_ROOT)/control/libblktapctl.a:
	make -C $(BLKTAP_ROOT)/control libblktapctl.a

$(IBIN): $(XENIO-OBJS) xenio.o $(BLKTAP_ROOT)/control/libblktapctl.a
	$(CC) -o $@ $^ $(LDFLAGS)

$(LIB): $(XENIO-OBJS)
	$(AR) r $@ $^

install: all
	$(INSTALL_DIR) -p $(DESTDIR)$(INST_DIR)
	$(INSTALL_PROG) $(IBIN) $(DESTDIR)$(INST_DIR)

clean:
	rm -f *.o *.o.d .*.o.d $(IBIN) $(LIB)

.PHONY: clean install
