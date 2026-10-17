// Exercise Wine's asynchronous connect event before sending any HTTP bytes.
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data)) return 1;
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    WSAEVENT event = WSACreateEvent();
    int passed = 0;
    if (socketHandle == INVALID_SOCKET || event == WSA_INVALID_EVENT) goto cleanup;
    if (WSAEventSelect(socketHandle, event, FD_CONNECT | FD_READ | FD_CLOSE)) goto cleanup;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(80);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int result = connect(socketHandle, (struct sockaddr*)&address, sizeof(address));
    int connectError = WSAGetLastError();
    printf("BW_TCP connect=%d error=%d\n", result, connectError);
    if (result != SOCKET_ERROR || connectError != WSAEWOULDBLOCK) goto cleanup;
    DWORD wait = WSAWaitForMultipleEvents(1, &event, FALSE, 5000, FALSE);
    WSANETWORKEVENTS events = {0};
    if (wait != WSA_WAIT_EVENT_0 || WSAEnumNetworkEvents(socketHandle, event, &events)) goto cleanup;
    if (!(events.lNetworkEvents & FD_CONNECT) || events.iErrorCode[FD_CONNECT_BIT]) goto cleanup;
    printf("BW_TCP connected_event=1\n");
    // The test relay always forwards to its own HTTP server. This guest port
    // cannot select an arbitrary host destination.
    const char* request = "GET /__fixtures/winsock.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    size_t sent = 0, requestSize = strlen(request);
    DWORD until = GetTickCount() + 5000;
    while (sent < requestSize && GetTickCount() < until) {
        result = send(socketHandle, request + sent, requestSize - sent, 0);
        if (result > 0) sent += result;
        else if (WSAGetLastError() != WSAEWOULDBLOCK) goto cleanup;
        else Sleep(10);
    }
    if (sent != requestSize) goto cleanup;
    char response[4096];
    size_t received = 0;
    until = GetTickCount() + 5000;
    while (received < sizeof(response) - 1 && GetTickCount() < until) {
        result = recv(socketHandle, response + received, sizeof(response) - 1 - received, 0);
        if (result > 0) {
            received += result;
            response[received] = 0;
            if (strstr(response, "200 OK") && strstr(response, "BW_TCP_HTTP_OK")) { passed = 1; break; }
        } else if (result == 0 || WSAGetLastError() != WSAEWOULDBLOCK) break;
        else Sleep(10);
    }
cleanup:
    if (socketHandle != INVALID_SOCKET) closesocket(socketHandle);
    if (event != WSA_INVALID_EVENT) WSACloseEvent(event);
    WSACleanup();
    printf("BW_TCP done=%d\n", passed);
    return passed ? 0 : 1;
}
