#include <stdio.h>
#include <string.h>

#define MD5_HASH_SIZE 33

int calculate_md5(FILE* fp, char* result, char* filename) {
    char command[256];
    char md5_output[MD5_HASH_SIZE];

    // Monta o comando para calcular o MD5
    snprintf(command, sizeof(command), "md5sum %s 2>/dev/null | awk '{print $1}'", filename);

    // Lê o hash gerado
    if (fgets(md5_output, sizeof(md5_output), fp) == NULL) {
        perror("Erro ao ler a saída do md5sum");
        pclose(fp);
        return -1;
    }

    // Remove possíveis quebras de linha no final do hash
    md5_output[strcspn(md5_output, "\n")] = 0;

    memcpy(result, md5_output, sizeof(md5_output));

    return 0;
}

int check_md5(FILE* fp, char *expected_md5, char* filename) {
  char result[MD5_HASH_SIZE];
  calculate_md5(fp, result, filename);
  return strcmp(result, expected_md5) == 0;
}
