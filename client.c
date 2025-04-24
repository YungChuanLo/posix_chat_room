#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>

#define BUFFER_SIZE 1024
#define NAME_LEN 32

char *trimwhitespace(char *str);
volatile sig_atomic_t keep_running = 1;
int sock = -1;

void handle_sigint(int sig) {
    keep_running = 0;
    printf("\nCtrl+C detected. Shutting down...\n");
    if (sock >= 0) {
        shutdown(sock, SHUT_WR);
    }
}

void *recv_handler(void *arg) {
    int sockfd = *(int *)arg;
    char buffer[BUFFER_SIZE];
    int nbytes;

    while (keep_running && (nbytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[nbytes] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }

    if (nbytes == 0 && keep_running) {
        printf("\nServer disconnected.\n");
    } else if (nbytes < 0 && errno != EINTR && keep_running) {
        perror("\nrecv failed");
    }

    keep_running = 0;
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
    if (argc != 4) {
        fprintf(stderr, "usage: %s <name> <host> <port>\n", argv[0]);
        return 1;
    }

    char *client_name = argv[1];
    char *hostname = argv[2];
    int port = validate_port(argv[3]);

    if (strlen(client_name) == 0) {
        fprintf(stderr, "Error: Name cannot be empty.\n");
        return 1;
    }
    if (strlen(client_name) >= NAME_LEN) {
        fprintf(stderr, "Error: Name is too long (max %d characters).\n", NAME_LEN - 1);
        return 1;
    }
    for (char *p = client_name; *p; p++) {
        if (isspace((unsigned char)*p)) {
            fprintf(stderr, "Error: Name cannot contain whitespace.\n");
            return 1;
        }
    }

    if (port < 0) {
        fprintf(stderr, "Error: '%s' is not a valid port (1-65535).\n", argv[3]);
        return 1;
    }

    struct hostent *he = gethostbyname(hostname);
    if (!he) {
        fprintf(stderr, "Error: Could not resolve hostname '%s'\n", hostname);
        herror("gethostbyname");
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        close(sock);
        sock = -1;
        return 1;
    }

    printf("Connected to server '%s' on port %d as '%s'.\n", hostname, port, client_name);

    char name_msg[NAME_LEN + 1];
    snprintf(name_msg, sizeof(name_msg), "%s\n", client_name);
    if (send(sock, name_msg, strlen(name_msg), 0) < 0) {
        perror("failed to send client name");
        close(sock);
        sock = -1;
        return 1;
    }

    signal(SIGINT, handle_sigint);

    pthread_t recv_tid;
    if (pthread_create(&recv_tid, NULL, recv_handler, &sock) != 0) {
        perror("pthread_create failed for receiver");
        close(sock);
        sock = -1;
        return 1;
    }

    char input_buffer[BUFFER_SIZE];
    printf("Enter messages or commands (/list, /pm <user> <msg>, /delay <sec> <user> <msg>, /quit):\n");
    while (keep_running) {
        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
            if (feof(stdin)) {
                printf("\nInput closed (EOF). Sending /quit command...\n");
            } else if (keep_running) {
                perror("fgets failed");
            }
            keep_running = 0;
            break;
        }

        input_buffer[strcspn(input_buffer, "\n")] = 0;
        char *trimmed_input = trimwhitespace(input_buffer);

        if (trimmed_input == NULL || strlen(trimmed_input) == 0) {
            continue;
        }

        if (strcmp(trimmed_input, "/quit") == 0 || strcmp(trimmed_input, "/exit") == 0) {
            keep_running = 0;
            break;
        }

        char send_buf[BUFFER_SIZE + 1];
        snprintf(send_buf, sizeof(send_buf), "%s\n", trimmed_input);

        if (keep_running && send(sock, send_buf, strlen(send_buf), 0) < 0) {
            if (errno != EPIPE && keep_running) {
                perror("send failed");
            } else if (keep_running) {
                fprintf(stderr, "Server connection lost while sending.\n");
            }
            keep_running = 0;
            break;
        }
    }

    printf("Disconnecting...\n");
    if (sock >= 0) {
        shutdown(sock, SHUT_WR);
    }

    pthread_join(recv_tid, NULL);

    if (sock >= 0) {
        close(sock);
        sock = -1;
    }

    printf("Exited.\n");
    return 0;
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
