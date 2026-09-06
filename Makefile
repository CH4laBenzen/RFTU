all:
	gcc src/client.c -o build/client
	gcc src/server.c -o build/server -pthread
clean:
	rm -rf build/server build/client