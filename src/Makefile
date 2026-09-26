MAKEFLAGS += -r
.PHONY: all clean extern
.SUFFIXES: .c .o .d
SRC := $(shell find . -path ./src -prune -o -type f -name "*.c" -print)
OBJ := $(patsubst %.c, %.o, $(SRC))
DEP := $(patsubst %.c, %.d, $(SRC))

EXT := cmacs.h include/uv.h lib/libuv.a
WGET := wget -qc --show-progress -t 3 --waitretry=3

CFLAGS ?= -O3 -fno-plt -pipe
CFLAGS += -D_GNU_SOURCE=1 -pthread -D_REENTRANT -fPIC -fwrapv -fms-extensions -Wall -Wvla -Wno-parentheses -Wno-microsoft -I$(CURDIR) -I$(CURDIR)/extern -I$(CURDIR)/include
LDFLAGS ?= -Wl,-O1
LDFLAGS += -L$(CURDIR)/lib
LDLIBS += -l:libuv.a -lm -lpthread

all: libcuv.a libcuv.so

libcuv.a: $(OBJ)
	$(AR) $(ARFLAGS) $@ $^

libcuv.so: $(OBJ)
	$(LINK.o) -shared $^ $(LOADLIBES) $(LDLIBS) -o $@

%.o: %.c
	$(COMPILE.c) $< -o $@

clean:
	$(RM) -- $(OBJ) $(DEP) libcuv.a libcuv.so

-include $(DEP)
%.d: %.c $(EXT)
	$(COMPILE.c) -MM $< -o $@

extern: $(EXT)
	install -d $@

cmacs.h:
	@$(WGET) -O $@ https://raw.github.com/ArcanusNEO/cmacs/master/cmacs.h || ($(RM) $@ && false)

include/uv.h lib/libuv.a&:
	@$(WGET) -O libuv.tar.gz https://api.github.com/repos/libuv/libuv/tarball || ($(RM) libuv.tar.gz && false)
	install -d src/libuv
	@tar -xf libuv.tar.gz -C src/libuv --strip-components=1
	@$(RM) libuv.tar.gz
	cd src/libuv && \
	./autogen.sh && \
	./configure \
		--prefix=$(CURDIR) \
		--disable-shared \
		--enable-static \
		--disable-dependency-tracking \
		CFLAGS="-O2 $$CFLAGS -fPIC" && \
	$(MAKE) && \
	$(MAKE) install

.SECONDARY: $(OBJ)
