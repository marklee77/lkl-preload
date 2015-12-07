LKL_PREFIX ?= ../lkl-linux/tools/lkl
LKL_INCLUDE_PATH ?= $(LKL_PREFIX)/include
LKL_LIB_PATH ?= $(LKL_PREFIX)/lib

lkl-preload.so: lkl-preload.o $(LKL_LIB_PATH)/liblkl.so
	gcc -o $@ -shared $^ -lpthread -lrt -ldl
	
lkl-preload.o: lkl-preload.c Makefile
	gcc -I$(LKL_INCLUDE_PATH) -Wall -g -O2 \
	    -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
	    -fno-strict-aliasing -fPIC -D_FILE_OFFSET_BITS=64 \
	    -c -o $@ $<

clean:
	rm -f *.o *.so
