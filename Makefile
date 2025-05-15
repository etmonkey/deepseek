CFLAGS = -Wall -g
SRCS = ds.c
OBJS = $(SRCS:.c=.o)
TARGETS = $(SRCS:.c=)

ds: ds.c
	gcc $(CFLAGS) -o ds ds.c -lcurl -lcjson

clean:
	rm -f $(OBJS) $(TARGETS)

