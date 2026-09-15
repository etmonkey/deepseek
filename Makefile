CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lcurl -lcjson
SRCS = ds.c tool_calls.c
OBJS = $(SRCS:.c=.o)
TARGET = ds

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ds.o: ds.c tool_calls.h config_json.h tool_calls_json.h global_vars.h
	$(CC) $(CFLAGS) -c $< -o $@

tool_calls.o: tool_calls.c tool_calls.h global_vars.h
	$(CC) $(CFLAGS) -c $< -o $@

config_json.h: ./config/config.json gen_config_header.py
	python3 gen_config_header.py

tool_calls_json.h: ./config/tool_calls.json gen_tool_calls_header.py
	python3 gen_tool_calls_header.py

clean:
	rm -f $(OBJS) $(TARGET)

rebuild: clean all

.PHONY: all clean rebuild