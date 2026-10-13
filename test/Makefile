MAKEFLAGS += -r
.PHONY: all check bench clean src-libs
.SUFFIXES: .c .o .d

SRC_DIR := $(CURDIR)/../src
INCLUDE_DIR := $(CURDIR)/../include
LIB_DIR := $(SRC_DIR)/lib
LIBCUV := $(SRC_DIR)/libcuv.a
LIBCUV_SO := $(SRC_DIR)/libcuv.so
LIBUV := $(LIB_DIR)/libuv.a

PROGRAMS := test stress portable fault scheduler shared-consumer
CHECK_PROGRAMS := test portable fault stress shared-consumer
SRC := $(wildcard *.c)
OBJ := $(patsubst %.c, %.o, $(SRC))
DEP := $(patsubst %.c, %.d, $(SRC))

FAULT_SRC := $(wildcard $(SRC_DIR)/*.c)
FAULT_OBJ := $(patsubst $(SRC_DIR)/%.c, .fault/%.o, $(FAULT_SRC))
FAULT_DEP := $(patsubst %.o, %.d, $(FAULT_OBJ))

CFLAGS ?= -O3 -fno-plt -pipe
CFLAGS += -D_GNU_SOURCE=1 -pthread -D_REENTRANT -fPIC -fwrapv -fms-extensions -Wall -Wvla -Wno-parentheses -Wno-microsoft -I$(SRC_DIR) -I$(SRC_DIR)/extern -I$(SRC_DIR)/include -I$(INCLUDE_DIR)
LDFLAGS ?= -Wl,-O1
LDFLAGS += -L$(SRC_DIR) -L$(LIB_DIR)
LDLIBS += -l:libuv.a -lm -lpthread

all: $(PROGRAMS)

check: $(CHECK_PROGRAMS)
	./test
	./portable
	./fault
	./stress
	./shared-consumer

bench: scheduler
	./scheduler

src-libs:
	$(MAKE) -C $(SRC_DIR)

$(LIBCUV) $(LIBCUV_SO) $(LIBUV): | src-libs

$(OBJ) $(DEP) $(FAULT_OBJ) $(FAULT_DEP): | src-libs

test stress portable scheduler: %: %.o $(LIBCUV) $(LIBUV)
	$(LINK.o) $< $(LIBCUV) $(LOADLIBES) $(LDLIBS) -o $@

shared-consumer: shared-consumer.o $(LIBCUV_SO)
	$(LINK.o) $< $(LOADLIBES) -lcuv -Wl,-rpath,'$$ORIGIN/../src' -o $@

fault: fault.o $(FAULT_OBJ) $(LIBUV)
	$(LINK.o) $(filter-out $(LIBUV),$^) $(LOADLIBES) $(LDLIBS) -o $@

fault.o fault.d $(FAULT_OBJ) $(FAULT_DEP): CFLAGS += -DCUV_ENABLE_FAULT_INJECTION

%.o: %.c
	$(COMPILE.c) $< -o $@

%.d: %.c
	$(COMPILE.c) -MM $< -o $@

.fault:
	mkdir -p $@

.fault/%.o: $(SRC_DIR)/%.c | .fault
	$(COMPILE.c) $< -o $@

.fault/%.d: $(SRC_DIR)/%.c | .fault
	$(COMPILE.c) -MM -MT "$(@:.d=.o)" $< -o $@

clean:
	$(RM) -- $(OBJ) $(DEP) $(FAULT_OBJ) $(FAULT_DEP) $(PROGRAMS)
	$(RM) -r -- .fault

-include $(DEP) $(FAULT_DEP)
