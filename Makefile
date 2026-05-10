CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wno-format-truncation -pedantic
LDFLAGS ?=
LDLIBS ?= -lncurses

TARGET = 42-backup-tool
SRC = src/main.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)
