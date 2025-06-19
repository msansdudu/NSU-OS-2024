#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#define BUFFER_SIZE 4096
#define LINES_PER_SCREEN 25

int line_count = 0;
char *data_buffer = NULL;
size_t data_size = 0;
size_t data_capacity = 0;
size_t print_offset = 0;
int prompt_shown = 0;

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

    if ((port_pos && (port_pos - host_start > 256)) || ((path_start && !port_pos) && (path_start - host_start > 256))) {
       fprintf(stderr, "Too long hostname!\n");
       exit(1);
    }

    if (port_pos && (!path_start || port_pos < path_start)) {
        strncpy(host, host_start, port_pos - host_start);
        host[port_pos - host_start] = '\0';
        port = atoi(port_pos + 1);
        if (path_start) {
            strcpy(path, path_start);
        }
    } else if (path_start) {
        strncpy(host, host_start, path_start - host_start);
        host[path_start - host_start] = '\0';
        strcpy(path, path_start);
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

void printing_lines(int *paused) {
    if (print_offset >= data_size || *paused){
        return;
    }
    size_t i = print_offset;
    int lines_printed = 0;

    while (i < data_size && lines_printed < LINES_PER_SCREEN - line_count) {
        size_t line_end = i;
        while (line_end < data_size && data_buffer[line_end] != '\n') {
            line_end++;
        }

        fwrite(&data_buffer[i], 1, line_end - i + (line_end < data_size ? 1 : 0), stdout);
        fflush(stdout);
        lines_printed++;
        line_count++;

        i = line_end + (line_end < data_size ? 1 : 0);
        print_offset = i;
        if (line_count >= LINES_PER_SCREEN) {
            if (!prompt_shown) {
                printf("\nPress space to scroll down\n");
                fflush(stdout);
                prompt_shown = 1;
            }
            *paused = 1;
            break;
        }
    }
}

void finish_printing(int paused) {
    while (print_offset < data_size) {
        if (paused && !prompt_shown) {
            printf("\nPress space to scroll down\n");
            fflush(stdout);
            prompt_shown = 1;
        }
        if (paused) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) > 0 && ch == ' ') {
                paused = 0;
                line_count = 0;
                prompt_shown = 0;
            } else {
                continue;
            }
        }
        printing_lines(&paused);
    }
    free(data_buffer);
    data_buffer = NULL;
    data_size = 0;
    data_capacity = 0;
    print_offset = 0;
    prompt_shown = 0;
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
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: SimpleHTTPClient/1.0\r\nConnection: close\r\n\r\n",
             path, host);
    send(sockfd, request, strlen(request), 0);

    int server_closed = 0;
    int paused = 0;
    char buffer[BUFFER_SIZE];
    int headers_skipped = 0;
    char *header_end = NULL;
    char *temp_buffer = NULL;
    size_t temp_size = 0;
    size_t temp_capacity = 0;

    while (!server_closed) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = max(sockfd, STDIN_FILENO) + 1;
        if (select(maxfd, &readfds, NULL, NULL, NULL) < 0) {
            fprintf(stderr, "Error with select\n");
            return EXIT_FAILURE;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char ch;
            read(STDIN_FILENO, &ch, 1);
            if (paused && ch == ' ') {
                paused = 0;
                line_count = 0;
                prompt_shown = 0;
                printing_lines(&paused);
            }
        }

        if (FD_ISSET(sockfd, &readfds)) {
            int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (bytes == 0 || (bytes < 0 && errno == ECONNRESET)) {
                if (!headers_skipped && temp_size > 0) {
                    temp_buffer[temp_size] = '\0';
                    fprintf(stderr, "Received headers (no body):\n%s\n", temp_buffer);
                }
                printf("\n\n\tConnection closed by server.\n");
                printf("\tFinished receiving data.\n\n");
                server_closed = 1;
                continue;
            }
            if (bytes < 0) {
                perror("Error with recv");
                return EXIT_FAILURE;
            }

            if (temp_size + bytes > temp_capacity) {
                temp_capacity = temp_size + bytes + BUFFER_SIZE;
                temp_buffer = realloc(temp_buffer, temp_capacity);
                if (!temp_buffer) {
                    fprintf(stderr, "Memory allocation error\n");
                    return EXIT_FAILURE;
                }
            }
            memcpy(temp_buffer + temp_size, buffer, bytes);
            temp_size += bytes;
            temp_buffer[temp_size] = '\0';

            if (!headers_skipped) {
                header_end = strstr(temp_buffer, "\r\n\r\n");
                if (!header_end) {
                    header_end = strstr(temp_buffer, "\n\n");
                    if (header_end) {
                        headers_skipped = 1;
                        size_t body_start = (header_end - temp_buffer) + 2;
                        size_t body_size = temp_size - body_start;
                        if (body_size > 0) {
                            if (data_size + body_size > data_capacity) {
                                data_capacity = data_size + body_size + BUFFER_SIZE;
                                data_buffer = realloc(data_buffer, data_capacity);
                                if (!data_buffer) {
                                    fprintf(stderr, "Memory allocation error\n");
                                    return EXIT_FAILURE;
                                }
                            }
                            memcpy(data_buffer + data_size, temp_buffer + body_start, body_size);
                            data_size += body_size;
                            if (!paused) {
                                printing_lines(&paused);
                            }
                        }
                    }
                } else {
                    headers_skipped = 1;
                    size_t body_start = (header_end - temp_buffer) + 4;
                    size_t body_size = temp_size - body_start;
                    if (body_size > 0) {
                        if (data_size + body_size > data_capacity) {
                            data_capacity = data_size + body_size + BUFFER_SIZE;
                            data_buffer = realloc(data_buffer, data_capacity);
                            if (!data_buffer) {
                                fprintf(stderr, "Memory allocation error\n");
                                return EXIT_FAILURE;
                            }
                        }
                        memcpy(data_buffer + data_size, temp_buffer + body_start, body_size);
                        data_size += body_size;
                        if (!paused) {
                            printing_lines(&paused);
                        }
                    }
                }
            } else {
                if (data_size + bytes > data_capacity) {
                    data_capacity = data_size + bytes + BUFFER_SIZE;
                    data_buffer = realloc(data_buffer, data_capacity);
                    if (!data_buffer) {
                        fprintf(stderr, "Memory allocation error\n");
                        return EXIT_FAILURE;
                    }
                }
                memcpy(data_buffer + data_size, buffer, bytes);
                data_size += bytes;
                if (!paused) {
                    printing_lines(&paused);
                }
            }
        }
    }

    free(temp_buffer);
    finish_printing(paused);
    close(sockfd);
    return EXIT_SUCCESS;
}