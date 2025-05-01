CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin

TARGET := nsexec

SRC := nsexec.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	install -D -m 4755 -o root -g root $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
