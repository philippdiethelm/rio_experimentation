
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")

constexpr unsigned short UDP_DST_PORT = 0x4321;
constexpr unsigned short UDP_SRC_PORT = 0x1234;

int main()
{
    WSADATA wsaData {};
    // Initialize Winsock
    if (int result = WSAStartup(MAKEWORD(2, 2), &wsaData); result != 0) {
        std::cout << "WSAStartup failed: " << result << std::endl;
        return 1;
    }

    // UDP socket
    auto sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // Bind to receive port and host
    sockaddr_in bind_addr {
        .sin_family = AF_INET,
        .sin_port = htons(UDP_DST_PORT),
        .sin_addr = {.S_un = {.S_addr = htonl(INADDR_ANY)}},
    };

    if (SOCKET_ERROR == bind(sockfd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr))) {
        std::cout << "bind failed with error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    std::cout << "Ready to receive data on UDP port " << UDP_DST_PORT << std::endl;

    for (;;) {
        char buffer[1024];

        // try to receive data
        auto received = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, 0);

        if (received <= 0) {
            std::cout << "recvfrom failed with error: " << WSAGetLastError() << std::endl;
            return 1;
        }

        std::cout << "Received " << received << " Bytes" << std::endl;
    }

    return 0;
}
