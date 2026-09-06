CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -pthread

all: build/client build/server

build/client: src/client.c common/network_simulator.c
	@mkdir -p build
	$(CC) $(CFLAGS) src/client.c common/network_simulator.c -o build/client

build/server: src/server.c common/network_simulator.c
	@mkdir -p build
	$(CC) $(CFLAGS) src/server.c common/network_simulator.c -o build/server

clean:
	rm -rf build