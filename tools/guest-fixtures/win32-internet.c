#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static void report(const char *format, ...) {
    FILE *file = fopen("Z:\\home\\username\\internet-result.txt", "a");
    if (!file) return;
    va_list arguments;
    va_start(arguments, format); vfprintf(file, format, arguments); va_end(arguments);
    fclose(file);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    WSADATA startup;
    if (WSAStartup(MAKEWORD(2, 2), &startup)) return 1;
    struct addrinfo hints = {0}, *addresses = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int result = getaddrinfo("example.com", "80", &hints, &addresses);
    report("WINBOX_INTERNET dns=%d\n", result);
    if (result || !addresses) return 2;
    SOCKET connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connection == INVALID_SOCKET) return 3;
    result = connect(connection, addresses->ai_addr, (int)addresses->ai_addrlen);
    freeaddrinfo(addresses);
    report("WINBOX_INTERNET connect=%d error=%d\n", result, result ? WSAGetLastError() : 0);
    if (result) return 4;
    const char *request = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    int sent = 0, length = (int)strlen(request);
    while (sent < length) {
        result = send(connection, request + sent, length - sent, 0);
        if (result <= 0) return 5;
        sent += result;
    }
    char response[4096] = {0};
    int received = 0, passed = 0;
    while (received < sizeof(response) - 1) {
        result = recv(connection, response + received, sizeof(response) - 1 - received, 0);
        if (result <= 0) break;
        received += result;
        if (strstr(response, "HTTP/1.1 200 OK")) { passed = 1; break; }
    }
    closesocket(connection);
    WSACleanup();
    report("WINBOX_INTERNET http200=%d bytes=%d\n", passed, received);
    return passed ? 0 : 6;
}
