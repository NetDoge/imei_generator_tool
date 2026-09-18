CC = gcc
CFLAGS = -Wall -O2
LDLIBS = -lsqlite3

imei_tool: imei_tool.c
	$(CC) $(CFLAGS) -o imei_tool imei_tool.c $(LDLIBS)

clean:
	rm -f imei_tool