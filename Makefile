CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags wayland-client)
LDFLAGS = $(shell pkg-config --libs wayland-client)

TARGET = my-layout
SRC = src/main.c src/river-layout-v3-protocol.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

src/river-layout-v3-protocol.c: protocol/river-layout-v3.xml
	wayland-scanner private-code < $< > $@

src/river-layout-v3-client-protocol.h: protocol/river-layout-v3.xml
	wayland-scanner client-header < $< > $@

src/main.c: src/river-layout-v3-client-protocol.h

clean:
	rm -f $(TARGET) $(OBJ) src/river-layout-v3-protocol.c src/river-layout-v3-client-protocol.h

.PHONY: all clean

