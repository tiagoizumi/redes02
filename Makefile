# Compiler and flags
all: servidor cliente

# Compile server
servidor: servidor.c rdt3.0.c
	gcc -Wall -g servidor.c rdt3.0.c hash.c -o servidor -lssl -lcrypto 

# Compile client
cliente: cliente.c rdt3.0.c
	gcc -Wall -g cliente.c rdt3.0.c hash.c -o cliente -lssl -lcrypto

# Run server
run_server: servidor
	./servidor 12345 teste2.txt

# Run client
run_client: cliente
	./cliente 127.0.0.1 12345 teste.txt

