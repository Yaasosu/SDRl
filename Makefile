CC ?= gcc
CFLAGS ?= -w -g

WAYLAND_CFLAGS = $(shell pkg-config --cflags wayland-client)
WAYLAND_LIBS = $(shell pkg-config --libs wayland-client)

SRCS = src/main.c src/river-window-management-v1-protocol.c src/river-xkb-bindings-v1-protocol.c
OBJS = $(SRCS:.c=.o)
TARGET = river-wm-client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(WAYLAND_LIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -Isrc $(WAYLAND_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean