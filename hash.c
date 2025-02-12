#include <openssl/md5.h>
#include <stdio.h>

void calculate_md5(FILE* file, unsigned char *hash) {
    MD5_CTX md5;
    MD5_Init(&md5);

    unsigned char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        MD5_Update(&md5, buffer, bytes_read);
    }

    MD5_Final(hash, &md5);
}

void hash_to_hex(unsigned char *hash, char *hex_hash, int hash_length) {
    for (int i = 0; i < hash_length; i++) {
        sprintf(hex_hash + (i * 2), "%02x", hash[i]);
    }
    hex_hash[hash_length * 2] = '\0';
}
