XEN_ROOT = $(CURDIR)/../..
include $(XEN_ROOT)/tools/Rules.mk

# FIXME -Wno-sign-compare generates a lot of warnings
override CFLAGS = \
	-Wall \
	-Wextra \
	-Wmissing-include-dirs \
	-Wunused \
	-Wuninitialized \
	-Werror \
	-Wunused-macros \
	-Wno-sign-compare \
export CFLAGS

LDLIBS += $(LDLIBS_libxenctrl)

override CPPCHECK_DIR ?= .

SUBDIRS-y :=
SUBDIRS-y += include
SUBDIRS-y += lvm
SUBDIRS-y += vhd
SUBDIRS-y += part
SUBDIRS-y += control
SUBDIRS-y += xenio
SUBDIRS-y += drivers

tags:
	ctags -R --language-force=C --c-kinds=+px

clean:
	rm -rf *.a *.so *.o *.rpm $(LIB) *~ $(DEPS) TAGS tags

check:
	cppcheck --enable=all -q $(CPPCHECK_DIR)

.PHONY: all clean install tags check
all clean install: %: subdirs-%
