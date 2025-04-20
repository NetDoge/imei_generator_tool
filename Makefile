
CC = gcc
CFLAGS = -Wall -O2 -sqlite3

imei_tool: imei_tool.c
	$(CC) $(CFLAGS) -o imei_tool imei_tool.c -lsqlite3

clean:
	rm -f imei_tool
