# ediFabric Native X12 — C examples
#
#   make                 # build example_all_functions
#   ./example_all_functions
#
# Put edifabric-x12-tools.dll/.so/.dylib in this directory (or pass --lib).

CC       ?= cc
CFLAGS   ?= -std=c99 -Wall -Wextra -O2
LDFLAGS  ?=
LDLIBS   ?=

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(UNAME_S),Linux)
  LDLIBS += -ldl
endif
ifeq ($(UNAME_S),Darwin)
  LDLIBS += -ldl
endif

.PHONY: all clean run

all: example_all_functions

edifabric_x12.o: edifabric_x12.c edifabric_x12.h c-abi-edifabric_x12_tools.h
	$(CC) $(CFLAGS) -c edifabric_x12.c -o edifabric_x12.o

example_all_functions.o: example_all_functions.c edifabric_x12.h
	$(CC) $(CFLAGS) -c example_all_functions.c -o example_all_functions.o

example_all_functions: example_all_functions.o edifabric_x12.o
	$(CC) $(CFLAGS) example_all_functions.o edifabric_x12.o -o example_all_functions $(LDFLAGS) $(LDLIBS)

run: example_all_functions
	./example_all_functions

clean:
	rm -f example_all_functions example_all_functions.o edifabric_x12.o edifabric.log
