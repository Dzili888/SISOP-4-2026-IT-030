#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9000

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    char input[1024];

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Error membuat socket \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nAlamat IP tidak valid/tidak didukung \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nKoneksi Gagal! Pastikan container Docker sudah berjalan.\n");
        return -1;
    }

    printf("==========================================\n");
    printf(" Terhubung ke Mini Database Service MOO!  \n");
    printf(" Ketik 'HELP' untuk melihat command.      \n");
    printf(" Ketik 'exit' untuk keluar.               \n");
    printf("==========================================\n");

    while (1) {
        printf("\n> ");
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0; 

        if (strcmp(input, "exit") == 0) {
            break;
        }

        send(sock, input, strlen(input), 0);
        memset(buffer, 0, sizeof(buffer));
        
        int valread = read(sock, buffer, 1024);
        if (valread > 0) {
            printf("%s\n", buffer);
        } else {
            printf("Server terputus.\n");
            break;
        }
    }

    close(sock);
    return 0;
}
