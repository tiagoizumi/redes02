# Compiler and flags
all: servidor cliente

# Compile server
servidor: servidor.c rdt3.0.c
	gcc -Wall -g servidor.c rdt3.0.c -o servidor

# Compile client
cliente: cliente.c rdt3.0.c
	gcc -Wall -g cliente.c rdt3.0.c -o cliente

# Run server
run_server: servidor
	./servidor 12345

# Run client
run_client: cliente
	./cliente 127.0.0.1 12345

