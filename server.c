#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctype.h>

#define MAX_CLIENTS 50
#define BUFFER_SIZE 1024
#define NAME_LEN 32

typedef struct {
    int sockfd;
    char name[NAME_LEN];
    struct sockaddr_in addr;
} client_t;

typedef struct {
    int delay;
    char sender_name[NAME_LEN];
    char recipient_name[NAME_LEN];
    char message[BUFFER_SIZE];
} delay_args_t;

client_t *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_client(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int sockfd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->sockfd == sockfd) {
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void broadcast(int sender_sockfd, const char *sender_name, const char *message) {
    char send_buffer[BUFFER_SIZE + NAME_LEN + 3];
    snprintf(send_buffer, sizeof(send_buffer), "%s: %s\n", sender_name, message);
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->sockfd != sender_sockfd) {
            send(clients[i]->sockfd, send_buffer, strlen(send_buffer), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_private_message(int sender_sockfd, const char* sender_name, const char *recipient_name, const char *message) {
    char send_buffer[BUFFER_SIZE + NAME_LEN + 10];
    char error_buffer[BUFFER_SIZE];
    int recipient_sockfd = -1;
    int found = 0;

    if (message == NULL || strlen(message) == 0) {
        if (sender_sockfd > 0) {
            send(sender_sockfd, "Server: Cannot send an empty private message.\n", 44, 0);
        }
        return;
    }

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && strcmp(clients[i]->name, recipient_name) == 0) {
            recipient_sockfd = clients[i]->sockfd;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (found && recipient_sockfd > 0) {
        if (sender_sockfd > 0 && recipient_sockfd == sender_sockfd) {
            send(sender_sockfd, "Server: You cannot send a PM to yourself.\n", 41, 0);
            return;
        }
        snprintf(send_buffer, sizeof(send_buffer), "(PM from %s): %s\n", sender_name, message);
        if (send(recipient_sockfd, send_buffer, strlen(send_buffer), 0) < 0) {
            perror("pm send failed");
            if (sender_sockfd > 0) {
                snprintf(error_buffer, sizeof(error_buffer), "Server: Failed to send PM to %s.\n", recipient_name);
                send(sender_sockfd, error_buffer, strlen(error_buffer), 0);
            }
        }
    } else {
        if (sender_sockfd > 0) {
            snprintf(error_buffer, sizeof(error_buffer), "Server: User '%s' not found or is offline.\n", recipient_name);
            send(sender_sockfd, error_buffer, strlen(error_buffer), 0);
        } else {
            printf("Server: Delayed PM recipient '%s' not found for message from %s.\n", recipient_name, sender_name);
            fflush(stdout);
        }
    }
}

void list_clients(int requester_sockfd) {
    char list_buffer[BUFFER_SIZE] = "Server: Connected users:\n";
    int current_len = strlen(list_buffer);
    int count = 0;

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            ++count;
            int needed = snprintf(list_buffer + current_len, sizeof(list_buffer) - current_len, "- %s\n", clients[i]->name);
            if (needed < 0 || needed >= sizeof(list_buffer) - current_len) {
                strcat(list_buffer, "- ... (list truncated)\n");
                break;
            }
            current_len += needed;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (count == 0) {
        strcat(list_buffer, "(No users connected)\n");
    }
    send(requester_sockfd, list_buffer, strlen(list_buffer), 0);
}

char *trimwhitespace(char *str) {
    if (str == NULL) return NULL;
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void *delay_handler(void *arg) {
    delay_args_t *d_args = (delay_args_t *)arg;
    sleep(d_args->delay);
    printf("Server: Executing delayed PM from %s to %s after %d seconds.\n",
           d_args->sender_name, d_args->recipient_name, d_args->delay);
    fflush(stdout);
    send_private_message(0, d_args->sender_name, d_args->recipient_name, d_args->message);
    free(d_args);
    return NULL;
}

void *handle_client(void *arg) {
    client_t *cli = (client_t *)arg;
    char buffer[BUFFER_SIZE];
    char message_buffer[BUFFER_SIZE * 2] = {0};
    int nbytes;
    int buffer_len = 0;

    printf("Client joined: %s (%s:%d) with fd %d\n", cli->name,
           inet_ntoa(cli->addr.sin_addr), ntohs(cli->addr.sin_port), cli->sockfd);
    snprintf(buffer, sizeof(buffer), "Server: %s joined the chat room.\n", cli->name);
    broadcast(cli->sockfd, "Server", cli->name);

    while ((nbytes = recv(cli->sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[nbytes] = '\0';
        if (buffer_len + nbytes < sizeof(message_buffer)) {
            memcpy(message_buffer + buffer_len, buffer, nbytes);
            buffer_len += nbytes;
            message_buffer[buffer_len] = '\0';
        } else {
            fprintf(stderr, "Client %s message buffer overflow, discarding data.\n", cli->name);
            buffer_len = 0;
            message_buffer[0] = '\0';
            continue;
        }

        char *line_start = message_buffer;
        char *newline_pos;
        while ((newline_pos = strchr(line_start, '\n')) != NULL) {
            *newline_pos = '\0';
            char *current_line = trimwhitespace(line_start);
            if (*current_line) {
                printf("Received from %s: %s\n", cli->name, current_line);
                if (current_line[0] == '/') {
                    if (strcmp(current_line, "/list") == 0) {
                        list_clients(cli->sockfd);
                    } else if (strncmp(current_line, "/pm ", 4) == 0 || strncmp(current_line, "/send ", 6) == 0) {
                        char *saveptr = current_line;
                        char *recipient = strtok_r(saveptr + (current_line[1]=='p'?4:6), " ", &saveptr);
                        char *msg = saveptr;
                        if (recipient && *recipient) {
                            msg = trimwhitespace(msg);
                            if (*msg) {
                                send_private_message(cli->sockfd, cli->name, recipient, msg);
                            } else {
                                send(cli->sockfd, "Server: Usage /pm <recipient> <message>\n", 38, 0);
                            }
                        } else {
                            send(cli->sockfd, "Server: Usage /pm <recipient> <message>\n", 38, 0);
                        }
                    } else if (strncmp(current_line, "/delay ", 7) == 0) {
                        char *saveptr = current_line;
                        char *time_str = strtok_r(saveptr + 7, " ", &saveptr);
                        char *recipient = strtok_r(NULL, " ", &saveptr);
                        char *msg = saveptr;
                        if (time_str && recipient && *time_str && *recipient) {
                            char *endptr;
                            errno = 0;
                            long delay_val = strtol(time_str, &endptr, 10);
                            if (!errno && *endptr=='\0' && delay_val>0 && delay_val<=86400) {
                                msg = trimwhitespace(msg);
                                if (*msg) {
                                    delay_args_t *d = malloc(sizeof(delay_args_t));
                                    if (d) {
                                        d->delay = (int)delay_val;
                                        strncpy(d->sender_name, cli->name, NAME_LEN-1);
                                        d->sender_name[NAME_LEN-1] = '\0';
                                        strncpy(d->recipient_name, recipient, NAME_LEN-1);
                                        d->recipient_name[NAME_LEN-1] = '\0';
                                        strncpy(d->message, msg, BUFFER_SIZE-1);
                                        d->message[BUFFER_SIZE-1] = '\0';
                                        pthread_t tid;
                                        if (pthread_create(&tid, NULL, delay_handler, d) == 0) {
                                            pthread_detach(tid);
                                            char confirm[100];
                                            snprintf(confirm, sizeof(confirm),
                                                     "Server: Message to %s scheduled in %ld seconds.\n",
                                                     recipient, delay_val);
                                            send(cli->sockfd, confirm, strlen(confirm), 0);
                                        } else {
                                            free(d);
                                            send(cli->sockfd, "Server: Failed to schedule message.\n", 35, 0);
                                        }
                                    } else {
                                        send(cli->sockfd, "Server: Internal error processing delay.\n", 39, 0);
                                    }
                                } else {
                                    send(cli->sockfd, "Server: Usage /delay <time_seconds> <recipient> <message>\n", 60, 0);
                                }
                            } else {
                                send(cli->sockfd, "Server: Invalid time (must be positive integer seconds, max 86400).\n", 70, 0);
                            }
                        } else {
                            send(cli->sockfd, "Server: Usage /delay <time_seconds> <recipient> <message>\n", 60, 0);
                        }
                    } else {
                        send(cli->sockfd, "Server: Unknown command.\n", 24, 0);
                    }
                } else {
                    broadcast(cli->sockfd, cli->name, current_line);
                }
            }
            line_start = newline_pos + 1;
        }
        if (line_start < message_buffer + buffer_len) {
            memmove(message_buffer, line_start, buffer_len - (line_start - message_buffer));
            buffer_len -= (line_start - message_buffer);
            message_buffer[buffer_len] = '\0';
        } else {
            buffer_len = 0;
            message_buffer[0] = '\0';
        }
    }

    if (nbytes == 0) {
        printf("Client disconnected: %s (fd %d)\n", cli->name, cli->sockfd);
        snprintf(buffer, sizeof(buffer), "Server: %s left the chat room.\n", cli->name);
        broadcast(cli->sockfd, "Server", cli->name);
    } else {
        perror("recv failed");
        printf("Client error: %s (fd %d)\n", cli->name, cli->sockfd);
        snprintf(buffer, sizeof(buffer), "Server: %s left due to an error.\n", cli->name);
        broadcast(cli->sockfd, "Server", cli->name);
    }

    close(cli->sockfd);
    remove_client(cli->sockfd);
    free(cli);
    pthread_detach(pthread_self());
    return NULL;
}

int validate_port(const char *s) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || v < 1 || v > 65535) {
        return -1;
    }
    return (int)v;
}

int main(int argc, char *argv[]) {
    if (argc != 2) { 
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = validate_port(argv[1]);
    if (port < 0) {
        fprintf(stderr, "'%s' is not a valid port (1-65535).\n", argv[1]);
        return 1;
    }

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        clients[i] = NULL;
    }

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket creation failed");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, 5) < 0) {
        perror("listen failed");
        close(server_sock);
        return 1;
    }

    printf("Server listening on port %d...\n", port);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&cli_addr, &cli_len);
        if (client_sock < 0) {
            perror("accept failed");
            continue;
        }

        char name_buf[NAME_LEN];
        int name_bytes = recv(client_sock, name_buf, NAME_LEN - 1, 0);
        if (name_bytes <= 0) {
            fprintf(stderr, "Failed to get client name or client disconnected.\n");
            close(client_sock);
            continue;
        }
        name_buf[name_bytes] = '\0';
        char *clean_name = trimwhitespace(name_buf);
        if (*clean_name == '\0') {
            fprintf(stderr, "Client sent empty name.\n");
            close(client_sock);
            continue;
        }

        client_t *cli = malloc(sizeof(client_t));
        if (!cli) {
            perror("malloc failed for client struct");
            close(client_sock);
            continue;
        }
        cli->sockfd = client_sock;
        cli->addr = cli_addr;
        strncpy(cli->name, clean_name, NAME_LEN - 1);
        cli->name[NAME_LEN - 1] = '\0';

        add_client(cli);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, cli) != 0) {
            perror("pthread_create failed");
            remove_client(cli->sockfd);
            free(cli);
            close(client_sock);
        }
    }

    close(server_sock);
    pthread_mutex_destroy(&clients_mutex);
    return 0;
}
