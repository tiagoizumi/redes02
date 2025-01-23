# redes02

gcc servidor.c rdt2.2.c -o servidor
gcc cliente.c rdt2.2.c -o cliente
./servidor 12345

./cliente 127.0.0.1 12345