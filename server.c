#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT 9000
#define MAX_CLIENTS 64
#define BUF_SIZE 1024

typedef struct {
    int fd;
    char nick[64];
} Client;

Client clients[MAX_CLIENTS];
int nclients = 0;

void broadcast(int sender, const char *msg, int len) {
    for (int i = 0; i < nclients; i++) {
        if (clients[i].fd != sender) {
            send(clients[i].fd, msg, len, 0);
        }
    }
}

int main(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("RTC server on port %d\n", PORT);

    fd_set readfds;
    char buf[BUF_SIZE];

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int maxfd = server_fd;

        for (int i = 0; i < nclients; i++) {
            FD_SET(clients[i].fd, &readfds);
            if (clients[i].fd > maxfd) maxfd = clients[i].fd;
        }

        select(maxfd + 1, &readfds, NULL, NULL, NULL);

        if (FD_ISSET(server_fd, &readfds)) {
            int fd = accept(server_fd, NULL, NULL);
            if (nclients < MAX_CLIENTS) {
                clients[nclients].fd = fd;
                strcpy(clients[nclients].nick, "anon");
                nclients++;
                printf("Client %d connected\n", fd);
            }
        }

        for (int i = 0; i < nclients; i++) {
            int fd = clients[i].fd;
            if (FD_ISSET(fd, &readfds)) {
                int n = recv(fd, buf, BUF_SIZE - 1, 0);
                if (n <= 0) {
                    close(fd);
                    clients[i] = clients[--nclients];
                    i--;
                } else {
                    buf[n] = '\0';

                    if (strncmp(buf, "NICK ", 5) == 0) {
                        char *name = buf + 5;
                        char *nl = strchr(name, '\n');
                        if (nl) *nl = '\0';
                        strncpy(clients[i].nick, name, 63);
                        clients[i].nick[63] = '\0';
                        printf("Client %d nick: %s\n", fd, clients[i].nick);
                    } else if (strncmp(buf, "MSG ", 4) == 0) {
                        char out[BUF_SIZE];
                        char *text = buf + 4;
                        char *nl = strchr(text, '\n');
                        if (nl) *nl = '\0';
                        snprintf(out, sizeof(out), "<%s> %s\n", clients[i].nick, text);
                        broadcast(fd, out, strlen(out));
                    }
                }
            }
        }
    }
    return 0;
}
