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

int parse_url(const char *url, char *host, char *path) {
    int port = 80;
    if (strncmp(url, "http://", 7) != 0) {
        fprintf(stderr, "Only http:// URLs are supported!\n");
        exit(1);
    }

    const char *host_start = url + 7;
    const char *path_start = strchr(host_start, '/');
    const char *port_pos = strchr(host_start, ':');

    if (port_pos - host_start > 256) {
        fprintf(stderr, "Too long hostname!\n");
        exit(1);
    }

    if (port_pos && (!path_start || port_pos < path_start)) {
        strncpy(host, host_start, port_pos - host_start);
        host[port_pos - host_start] = '\0';
        port = atoi(port_pos + 1);
    } else if (path_start) {
        strncpy(host, host_start, path_start - host_start);
        host[path_start - host_start] = '\0';
    } else {
        strcpy(host, host_start);
        strcpy(path, "/");
    }
    printf("Port: %d\n", port);
    return port;
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
    int port = parse_url(argv[1], host, path);
    int sockfd = create_connection(host, port);

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
    char *overflow = calloc(1, 1);
    size_t overflow_len = 0;

    while (1) {
        if (!paused && overflow_len) {
            char *line = strtok(overflow, "\n");
            char *remaining = NULL;
            while (line) {
                remaining = strtok(NULL, "\n");
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
                line = remaining;
            }
            if (!paused) {
                size_t rem_len = strlen(remaining ? remaining : "");
                memmove(overflow, remaining ? remaining : "", rem_len + 1);
                overflow_len = rem_len;
                char *shr = realloc(overflow, overflow_len + 1);
                if (shr) overflow = shr;
            }
        }

        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = max(sockfd, STDIN_FILENO) + 1;
        if (select(maxfd, &readfds, NULL, NULL, NULL) < 0) {
            fprintf(stderr, "Error with select\n");
            free(overflow);
            return EXIT_FAILURE;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char ch;
            read(STDIN_FILENO, &ch, 1);
            if (paused && ch == ' ') {
                paused = 0;
                line_count = 0;
                if (overflow_len && !paused) {
                    char *line = strtok(overflow, "\n");
                    char *remaining = NULL;
                    while (line) {
                        remaining = strtok(NULL, "\n");
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
                        line = remaining;
                    }
                    if (!paused) {
                        size_t rem_len = strlen(remaining ? remaining : "");
                        memmove(overflow, remaining ? remaining : "", rem_len + 1);
                        overflow_len = rem_len;
                        char *shr = realloc(overflow, overflow_len + 1);
                        if (shr) overflow = shr;
                    }
                }
            }
        }

        if (FD_ISSET(sockfd, &readfds)) {
            int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) break;
            buffer[bytes] = '\0';

            char *new_overflow = realloc(overflow, overflow_len + bytes + 1);
            if (!new_overflow) {
                fprintf(stderr, "Memory allocation error\n");
                free(overflow);
                return EXIT_FAILURE;
            }
            overflow = new_overflow;
            memcpy(overflow + overflow_len, buffer, bytes + 1);
            overflow_len += bytes;
            if (paused) continue;

            char *line = strtok(overflow, "\n");
            char *remaining = NULL;
            while (line) {
                remaining = strtok(NULL, "\n");

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
                line = remaining;
            }

            if (remaining || overflow[overflow_len - 1] != '\n') {
                size_t rem_len = strlen(remaining ? remaining : "");
                memmove(overflow, remaining ? remaining : "", rem_len + 1);
                overflow_len = rem_len;
                char *shrunk = realloc(overflow, overflow_len + 1);
                if (shrunk) overflow = shrunk;
            } else {
                overflow[0] = '\0';
                overflow_len = 0;
                char *shrunk = realloc(overflow, 1);
                if (shrunk) {
                    overflow = shrunk;
                }
            }
        }
    }

    int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes == 0) {
        printf("\n\n\tConnection closed by server.\n");
    } else if (bytes < 0) {
        fprintf(stderr, "Error with recv\n");
        free(overflow);
        return EXIT_FAILURE;
    }
    printf("\tFinished receiving data. Exiting.\n\n");

    free(overflow);
    close(sockfd);
    return EXIT_SUCCESS;
}