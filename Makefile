# Compiler and flags
CC = gcc
CFLAGS = -Wall -g

# Source files and executables
SRV_SRC = servidor.c rdt2.2.c
CLI_SRC = cliente.c rdt2.2.c
SRV_BIN = servidor
CLI_BIN = cliente

# Run parameters
PORT = 12345
IP = 127.0.0.1

# Default target
all: $(SRV_BIN) $(CLI_BIN)

# Compile server
$(SRV_BIN): $(SRV_SRC)
	$(CC) $(CFLAGS) $(SRV_SRC) -o $(SRV_BIN)

# Compile client
$(CLI_BIN): $(CLI_SRC)
	$(CC) $(CFLAGS) $(CLI_SRC) -o $(CLI_BIN)

# Run server
run_server: $(SRV_BIN)
	./$(SRV_BIN) $(PORT)

# Run client
run_client: $(CLI_BIN)
	./$(CLI_BIN) $(IP) $(PORT)

# Clean up
clean:
	rm -f $(SRV_BIN) $(CLI_BIN)
