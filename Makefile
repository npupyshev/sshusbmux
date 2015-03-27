CC = clang
CFLAGS = -framework CoreFoundation -F/System/Library/PrivateFrameworks \
-framework CoreFoundation -framework MobileDevice

all: main.c MobileDevice.h
	xcrun -sdk macosx $(CC) $(CFLAGS) main.c -o sshusbmux

install: sshusbmux
	cp sshusbmux /opt/local/bin
