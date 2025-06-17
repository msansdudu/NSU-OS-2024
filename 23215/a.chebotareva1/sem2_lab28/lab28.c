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
    if (sockfd < 0){
        fprintf(stderr, "Error with socket\n");
        return EXIT_FAILURE;
    }
    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
        fprintf(stderr, "Error with connection\n");
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

void wait_for_space() {
    printf("\nPress space to scroll down...\n");
    fflush(stdout);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    while (1) {
        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, NULL) > 0) {
            char ch;
            read(STDIN_FILENO, &ch, 1);
            if (ch == ' ') {
                break;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Needed only 1 arg - url with \"http:\" beginning");
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
    int line_count = 0;
    int in_headers = 1;
    fd_set readfds;
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        if (select(sockfd + 1, &readfds, NULL, NULL, NULL) <= 0) {
            break;
        }
        if (FD_ISSET(sockfd, &readfds)) {
            int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0){
                break;
            }
            buffer[bytes] = '\0';
            char *line = strtok(buffer, "\n");
            while (line) {
                if (in_headers) {
                    if (strcmp(line, "\r") == 0 || strcmp(line, "") == 0)
                        in_headers = 0;
                } else {
                    printf("%s\n", line);
                    line_count++;
                    if (line_count >= LINES_PER_SCREEN) {
                        wait_for_space();
                        line_count = 0;
                    }
                }
                line = strtok(NULL, "\n");
            }
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}