XEN_ROOT := $(CURDIR)/../../../
include $(XEN_ROOT)/tools/Rules.mk

BLKTAP_ROOT := ..

IBIN = part-util

override CFLAGS += \
	-I$(BLKTAP_ROOT)/include \
	$(CFLAGS_xeninclude)	

PART-OBJS := partition.o

all: $(IBIN) vhdpartx

$(IBIN): $(PART-OBJS) part-util.o
	$(CC) -o $@ $^ $(LDFLAGS)

install: all
	$(INSTALL_DIR) -p $(DESTDIR)$(SBINDIR)
	$(INSTALL_PROG) $(IBIN) $(DESTDIR)$(SBINDIR)
	$(INSTALL_PROG) vhdpartx $(DESTDIR)$(SBINDIR)

clean:
	rm -f *.o .*.o.d $(IBIN) $(PART-OBJS)

.PHONY: clean
