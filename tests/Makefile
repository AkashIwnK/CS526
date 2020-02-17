#Assumes that the PATH variable is already set to path to clang/clang++

CC = clang
CXX  = clang++

SOURCES = $(wildcard *.c)

OPTIMIZATION = -O0

CC_FLAGS =  `llvm-config --cflags --ldflags --libs --system-libs` $(OPTIMIZATION)

LL_FILES = $(SOURCES:%.c=%.ll)


#.SUFFIXES: .ll .c

.PHONY = all

all: $(LL_FILES)
	opt -load SROA.so -scalarrepl-akashk4 -dce -verify $< -o done.ll

%.ll: %.c
	$(CC) -emit-llvm -o $@ -c $< -w  $(CC_FLAGS)

