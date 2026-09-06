CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -pthread

SIMULATOR_SRC = src/network_simulator.c

all: build/client build/server

build/client: src/client.c $(SIMULATOR_SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) src/client.c $(SIMULATOR_SRC) -o build/client

build/server: src/server.c $(SIMULATOR_SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) src/server.c $(SIMULATOR_SRC) -o build/server

clean:
	rm -rf build