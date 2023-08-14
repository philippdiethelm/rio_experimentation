
#include <span>
#include <iostream>
#include <chrono>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

#pragma comment(lib, "Ws2_32.lib")

constexpr unsigned short UDP_SRC_PORT = 0x1234;
constexpr unsigned short UDP_DST_PORT = 0x4321;

using namespace std::literals::chrono_literals;
constexpr auto sleep_time = 500ms;

int main()
{
    WSADATA wsaData {};

    // Initialize Winsock
    if (int result = WSAStartup(MAKEWORD(2, 2), &wsaData); result != 0) {
        std::cout << "WSAStartup failed with error " << result << std::endl;
        return 1;
    }

    // UDP socket
    auto sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET) {
        std::cout << "socket failed with error " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Setup UDP source port
    sockaddr_in bind_addr {
        .sin_family = AF_INET,
        .sin_port = htons(UDP_SRC_PORT),
    };

    if (SOCKET_ERROR == bind(sockfd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr))) {
        std::cout << "bind failed with error " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Destination port and address
    sockaddr_in send_addr {
        .sin_family = AF_INET,
        .sin_port = htons(UDP_DST_PORT),
        .sin_addr = {.S_un = {.S_addr = htonl(INADDR_LOOPBACK)}},
    };

    // Packet content
    struct {
        uint64_t number = 0;
        uint8_t data[128] {};
    } packet;

    auto packet_span = std::span {reinterpret_cast<char*>(&packet), sizeof(packet)};

    // ready
    for (;;) {

        packet.number++;

        std::cout << "Sending packet #: " << packet.number << std::endl;

        if (sendto(
                sockfd,
                packet_span.data(),
                packet_span.size_bytes(),
                0,
                reinterpret_cast<sockaddr*>(&send_addr),
                sizeof(send_addr)) != packet_span.size_bytes()) {
            std::cout << "Failed to send Packet: " << WSAGetLastError() << std::endl;
            return 1;
        }

        std::this_thread::sleep_for(sleep_time);
    }

    return 0;
}
