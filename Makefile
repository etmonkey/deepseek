CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lcurl -lcjson
SRCS = ds.c tool_calls.c
OBJS = $(SRCS:.c=.o)
TARGET = ds

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ds.o: ds.c tool_calls.h
	$(CC) $(CFLAGS) -c $< -o $@

tool_calls.o: tool_calls.c tool_calls.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

rebuild: clean all

.PHONY: all clean rebuild