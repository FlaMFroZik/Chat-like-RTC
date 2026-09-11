#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
typedef int socklen_t;
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
/* Для потоков на Windows */
#include <process.h>
#define THREAD_FUNC unsigned __stdcall
#define CREATE_THREAD(_func, _arg) _beginthreadex(NULL, 0, _func, _arg, 0, NULL)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#define THREAD_FUNC void*
#define CREATE_THREAD(_func, _arg) ({ pthread_t t; pthread_create(&t, NULL, _func, _arg); t; })
#endif

#define SERVER_IP "tcp.cloudpub.ru"
#define PORT 24176
#define BUF_SIZE 1024

int sock = -1;
char nick[64] = "anon";
volatile int running = 1;

void send_nick(void) {
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf), "NICK %s\n", nick);
    send(sock, buf, strlen(buf), 0);
}

void send_msg(const char *text) {
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf), "MSG %s\n", text);
    send(sock, buf, strlen(buf), 0);
}

/* Поток для чтения ввода пользователя */
THREAD_FUNC input_thread(void *arg) {
    (void)arg;
    char input[BUF_SIZE];

    while (running) {
        /* fgets блокируется, но это нормально для потока ввода */
        if (fgets(input, sizeof(input), stdin)) {
            size_t len = strlen(input);
            /* Удаляем \n и \r */
            while (len > 0 && (input[len-1] == '\n' || input[len-1] == '\r')) {
                input[--len] = '\0';
            }
            if (len == 0) continue;

            if (strncmp(input, "/nick ", 6) == 0) {
                strncpy(nick, input + 6, sizeof(nick) - 1);
                nick[sizeof(nick) - 1] = '\0';
                printf("Nick changed to: %s\n", nick);
                fflush(stdout);
                send_nick();
            } else {
                send_msg(input);
                printf("[You]: %s\n", input);
                fflush(stdout);
            }
        }
    }
    return 0;
}

int main(void) {
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Failed WSAStartup\n");
        return 1;
    }
    #endif

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct addrinfo hints = {0}, *res;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", PORT);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(SERVER_IP, port_str, &hints, &res) != 0) {
        fprintf(stderr, "Cannot resolve %s\n", SERVER_IP);
        return 1;
    }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) < 0) {
        perror("connect");
        freeaddrinfo(res);
        return 1;
    }
    freeaddrinfo(res);

    printf("Connected to chat server!\n");
    printf("Your nick: %s (type /nick newname to change)\n", nick);
    fflush(stdout);
    send_nick();

    /* Запускаем поток ввода */
    uintptr_t thread_id = CREATE_THREAD(input_thread, NULL);
    #ifdef _WIN32
    if (thread_id == 0) {
        #else
        if (pthread_create(NULL, NULL, input_thread, NULL) != 0) {
            #endif
            fprintf(stderr, "Failed to create input thread\n");
            close(sock);
            #ifdef _WIN32
            WSACleanup();
            #endif
            return 1;
        }

        char buffer[BUF_SIZE];
        while (running) {
            int n = recv(sock, buffer, BUF_SIZE - 1, 0);
            if (n <= 0) {
                printf("\n[Disconnected]\n");
                running = 0;
                break;
            }
            buffer[n] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        close(sock);

        #ifdef _WIN32
        /* Ждем завершения потока (упрощенно) */
        WaitForSingleObject((HANDLE)thread_id, INFINITE);
        _endthreadex(0);
        WSACleanup();
        #else
        pthread_join(/* нужно сохранить дескриптор */, NULL); /* в реальном коде сохрани pthread_t */
        #endif

        return 0;
    }
