CC = gcc
CFLAGS = -Wall -g

all: servidor cliente

server: servidor.c rdt3.0.c
	$(CC) $(CFLAGS) servidor.c rdt3.0.c -o servidor

client: cliente.c rdt3.0.c
	$(CC) $(CFLAGS) cliente.c rdt3.0.c -o cliente

run_server: servidor
	./servidor 12345


run_client: cliente
	./cliente 127.0.0.1 12345 data2.txt

clean:
	rm -f servidor cliente
