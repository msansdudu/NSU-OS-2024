#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define BUFFER_SIZE 4096
#define LINES_PER_SCREEN 25

int create_connection(const char *host, int port) {
    struct hostent *server = gethostbyname(host);
    if (!server) {
        fprintf(stderr, "No such host: %s\n", host);
        return EXIT_FAILURE;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error with socket\n");
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Error with socket\n");
        return EXIT_FAILURE;
    }

    return sockfd;
}

void parse_url(const char *url, char *host, char *path) {
    if (strncmp(url, "http://", 7) != 0) {
        fprintf(stderr, "Only http:// URLs are supported\n");
        exit(1);
    }

    const char *host_start = url + 7;
    const char *path_start = strchr(host_start, '/');
    if (path_start) {
        strncpy(host, host_start, path_start - host_start);
        host[path_start - host_start] = '\0';
        strcpy(path, path_start);
    } else {
        strcpy(host, host_start);
        strcpy(path, "/");
    }
}

int max(int a, int b) {
    return (a > b) ? a : b;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s http://host/path\n", argv[0]);
        return EXIT_FAILURE;
    }

    char host[256], path[1024];
    parse_url(argv[1], host, path);
    int sockfd = create_connection(host, 80);

    // Отправка HTTP-запроса
    char request[2048];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
             path, host);
    send(sockfd, request, strlen(request), 0);

    char buffer[BUFFER_SIZE];
    int in_headers = 1;
    int line_count = 0;
    int paused = 0;

    fd_set readfds;
    char overflow[BUFFER_SIZE] = {0};  // для хранения "недопарсенной" строки
    size_t overflow_len = 0;

    while (1) {
        FD_ZERO(&readfds);
        if (!paused)
            FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = max(sockfd, STDIN_FILENO) + 1;
        if (select(maxfd, &readfds, NULL, NULL, NULL) < 0) {
            fprintf(stderr, "Error with socket\n");
            return EXIT_FAILURE;
        }

        // пользователь нажал клавишу
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char ch;
            read(STDIN_FILENO, &ch, 1);
            if (paused && ch == ' ') {
                paused = 0;
                line_count = 0;
            }
        }

        // данные из сокета
        if (!paused && FD_ISSET(sockfd, &readfds)) {
            int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0){
                break;
            }
            buffer[bytes] = '\0';
            
            // соединяем с остатком предыдущей строки (если был)
            char combined[BUFFER_SIZE * 2];
            snprintf(combined, sizeof(combined), "%s%s", overflow, buffer);
            overflow[0] = '\0';
            overflow_len = 0;

            char *line = strtok(combined, "\n");
            while (line) {
                // если заголовки ещё не пропущены
                if (in_headers) {
                    if (strcmp(line, "\r") == 0 || strcmp(line, "") == 0)
                        in_headers = 0;
                } else {
                    printf("%s\n", line);
                    line_count++;
                    if (line_count >= LINES_PER_SCREEN) {
                        printf("\nPress space to scroll down...\n");
                        fflush(stdout);
                        paused = 1;
                        break;
                    }
                }
                line = strtok(NULL, "\n");
            }

            if (line == NULL && buffer[bytes - 1] != '\n') {
                strncpy(overflow, combined + strlen(combined), sizeof(overflow) - 1);
                overflow[sizeof(overflow) - 1] = '\0';
            }
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}