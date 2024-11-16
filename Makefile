LDLIBS = $(shell pkg-config --libs libusb-1.0)
CFLAGS = $(shell pkg-config --cflags libusb-1.0)

CFLAGS += -Werror -g3

heat:
