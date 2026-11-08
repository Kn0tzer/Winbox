// Resolve a host name through the guest's own name-resolution interface
// (Winsock getaddrinfo) and connect to the resolved result; then confirm
// an unresolvable name does not produce a working connection.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

// Event-based connect with a deadline, in the shape of the win32-tcp
// fixture: returns 0 when connected, a Winsock error code on failure,
// or -1 when the deadline passes with no outcome. The socket stays in
// event-select (nonblocking) mode; callers poll send/recv.
static int connect_timed(SOCKET socketHandle, const struct sockaddr* address, int length, DWORD timeoutMs, WSAEVENT* eventOut) {
    WSAEVENT event = WSACreateEvent();
    *eventOut = event;
    if (event == WSA_INVALID_EVENT) return WSAGetLastError();
    if (WSAEventSelect(socketHandle, event, FD_CONNECT) != 0) return WSAGetLastError();
    int result = connect(socketHandle, address, length);
    if (result == 0) return 0;
    if (WSAGetLastError() != WSAEWOULDBLOCK) return WSAGetLastError();
    DWORD wait = WSAWaitForMultipleEvents(1, &event, FALSE, timeoutMs, FALSE);
    if (wait != WSA_WAIT_EVENT_0) return -1;
    WSANETWORKEVENTS events = {0};
    if (WSAEnumNetworkEvents(socketHandle, event, &events) != 0) return WSAGetLastError();
    if (!(events.lNetworkEvents & FD_CONNECT)) return -1;
    return events.iErrorCode[FD_CONNECT_BIT];
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data)) { printf("BW_DNS done=0\n"); return 1; }
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int passed = 0, contained = 0;

    struct addrinfo* addresses = NULL;
    int gai = getaddrinfo("example.com", "80", &hints, &addresses);
    printf("BW_DNS gai=%d\n", gai);
    if (gai == 0 && addresses) {
        char text[64] = {0};
        struct sockaddr_in* sin = (struct sockaddr_in*)addresses->ai_addr;
        inet_ntop(AF_INET, &sin->sin_addr, text, sizeof(text));
        printf("BW_DNS addr=%s\n", text);
        SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socketHandle != INVALID_SOCKET) {
            WSAEVENT event = WSA_INVALID_EVENT;
            int connectError = connect_timed(socketHandle, addresses->ai_addr, (int)addresses->ai_addrlen, 30000, &event);
            printf("BW_DNS connect=%d\n", connectError);
            if (connectError == 0) {
                const char* request = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
                size_t sent = 0, requestSize = strlen(request);
                DWORD until = GetTickCount() + 20000;
                while (sent < requestSize && GetTickCount() < until) {
                    int result = send(socketHandle, request + sent, (int)(requestSize - sent), 0);
                    if (result > 0) sent += (size_t)result;
                    else if (WSAGetLastError() != WSAEWOULDBLOCK) break;
                    else Sleep(10);
                }
                char response[4096] = {0};
                size_t received = 0;
                int http = 0;
                until = GetTickCount() + 20000;
                while (received < sizeof(response) - 1 && GetTickCount() < until) {
                    int result = recv(socketHandle, response + received, (int)(sizeof(response) - 1 - received), 0);
                    if (result > 0) {
                        received += (size_t)result;
                        response[received] = 0;
                        if (strstr(response, "HTTP/1.1 200")) { http = 1; break; }
                    } else if (result == 0) break;
                    else if (WSAGetLastError() != WSAEWOULDBLOCK) break;
                    else Sleep(10);
                }
                printf("BW_DNS http=%d bytes=%d\n", http, (int)received);
                passed = http;
            }
            closesocket(socketHandle);
            if (event != WSA_INVALID_EVENT) WSACloseEvent(event);
        }
        freeaddrinfo(addresses);
    }

    // An unresolvable name must fail -- at resolution or at connect --
    // never silently produce a working connection.
    struct addrinfo* badAddresses = NULL;
    int badGai = getaddrinfo("bw64-nonexistent.invalid", "80", &hints, &badAddresses);
    printf("BW_DNS bad_gai=%d\n", badGai);
    if (badGai != 0) {
        contained = 1;
    } else if (badAddresses) {
        char text[64] = {0};
        struct sockaddr_in* sin = (struct sockaddr_in*)badAddresses->ai_addr;
        inet_ntop(AF_INET, &sin->sin_addr, text, sizeof(text));
        printf("BW_DNS bad_addr=%s\n", text);
        SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socketHandle != INVALID_SOCKET) {
            WSAEVENT event = WSA_INVALID_EVENT;
            int connectError = connect_timed(socketHandle, badAddresses->ai_addr, (int)badAddresses->ai_addrlen, 20000, &event);
            printf("BW_DNS bad_connect=%d\n", connectError);
            contained = connectError != 0;
            closesocket(socketHandle);
            if (event != WSA_INVALID_EVENT) WSACloseEvent(event);
        }
        freeaddrinfo(badAddresses);
    }
    WSACleanup();
    int done = passed && contained;
    printf("BW_DNS done=%d\n", done);
    return done ? 0 : 1;
}
