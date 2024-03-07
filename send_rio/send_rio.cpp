
#include <iostream>
#include <chrono>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <winsock2.h>
#include <mswsock.h>
#pragma comment(lib, "Ws2_32.lib")

constexpr unsigned short UDP_SRC_PORT = 0x1234;
constexpr unsigned short UDP_DST_PORT = 0x4321;

using namespace std::literals::chrono_literals;
constexpr auto sleep_time = 500ms;

// Packet content
struct Packet {
    uint64_t number = 0;
    uint8_t data[128] {};
};


int main()
{
    // Initialize Winsock
    // https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsastartup
    WSADATA wsaData {};
    if (int result = WSAStartup(MAKEWORD(2, 2), &wsaData); result != 0) {
        std::cout << "WSAStartup failed with error " << result << std::endl;
        return 1;
    }

    // UDP socket
    // https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasocketa
    // https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasocketw
    auto sockfd = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_REGISTERED_IO);
    if (sockfd == INVALID_SOCKET) {
        std::cout << "WSASocket failed with error " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Setup UDP source port
    // https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-bind
    sockaddr_in bind_addr {
        .sin_family = AF_INET,
        .sin_port = htons(UDP_SRC_PORT),
    };

    if (SOCKET_ERROR == bind(sockfd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr))) {
        std::cout << "bind failed with error " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Get RIO functions from API
    // https://learn.microsoft.com/en-us/windows/win32/api/mswsock/ns-mswsock-rio_extension_function_table
    // https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaioctl
    RIO_EXTENSION_FUNCTION_TABLE rio_extension_function_table {};
    GUID GUID_WSAID_MULTIPLE_RIO = WSAID_MULTIPLE_RIO;
    DWORD bytes_returned = 0;

    if (int result = WSAIoctl(
            sockfd,                                       // [in]  SOCKET           s,
            SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,  // [in]  DWORD            dwIoControlCode,
            &GUID_WSAID_MULTIPLE_RIO,                     // [in]  LPVOID           lpvInBuffer,
            sizeof(GUID_WSAID_MULTIPLE_RIO),              // [in]  DWORD            cbInBuffer,
            (void**)&rio_extension_function_table,        // [out] LPVOID           lpvOutBuffer,
            sizeof(rio_extension_function_table),         // [in]  DWORD            cbOutBuffer,
            &bytes_returned,                              // [out] LPDWORD          lpcbBytesReturned,
            nullptr,                                      // [in]  LPWSAOVERLAPPED  lpOverlapped,
            nullptr);  // [in]  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        result != 0) {
        auto last_error = ::GetLastError();
        std::cout << "WSAIoctl Error: " << last_error << std::endl;
        return 1;
    }

    // Setup completion by Event
    // https://learn.microsoft.com/en-us/windows/win32/api/mswsock/ns-mswsock-rio_notification_completion
    auto notification_event = WSACreateEvent();
    if (notification_event == WSA_INVALID_EVENT) {
        std::cout << "WSACreateEvent Error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    RIO_NOTIFICATION_COMPLETION completion_spec {
        .Type = RIO_EVENT_COMPLETION,
        .Event =
            {
                .EventHandle = notification_event,
                .NotifyReset = TRUE,
            },
    };

    // Setup completion queue
    // https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_riocreatecompletionqueue
    constexpr DWORD max_outstanding_requests = 128;

    auto completion_queue = rio_extension_function_table.RIOCreateCompletionQueue(
        max_outstanding_requests,  // DWORD                        QueueSize,
        &completion_spec);         // PRIO_NOTIFICATION_COMPLETION NotificationCompletion
    if (completion_queue == RIO_INVALID_CQ) {
        std::cout << "RIOCreateCompletionQueue Error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Setup request queue
    // https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_riocreaterequestqueue
    auto request_queue = rio_extension_function_table.RIOCreateRequestQueue(
        sockfd,                    // SOCKET   Socket,
        0,                         // ULONG    MaxOutstandingReceive,
        1,                         // ULONG    MaxReceiveDataBuffers,
        max_outstanding_requests,  // ULONG    MaxOutstandingSend,
        1,                         // ULONG    MaxSendDataBuffers,
        completion_queue,          // RIO_CQ   ReceiveCQ,
        completion_queue,          // RIO_CQ   SendCQ,
        nullptr);                  // PVOID    SocketContext
    if (request_queue == RIO_INVALID_RQ) {
        std::cout << "RIOCreateRequestQueue Error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Setup buffers
    constexpr size_t max_packet_length = 1024;
    constexpr size_t remote_address_length = sizeof(sockaddr_storage);
    constexpr size_t buffer_size = remote_address_length + max_outstanding_requests * max_packet_length;

    char* buffer = reinterpret_cast<char*>(malloc(buffer_size));
    if (buffer == nullptr) {
        std::cout << "Error allocating buffer of size " << buffer_size << std::endl;
        return 1;
    }

    // Register buffer
    // https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_rioregisterbuffer
    auto buffer_id = rio_extension_function_table.RIORegisterBuffer(
        buffer,                            // PCHAR DataBuffer,
        static_cast<DWORD>(buffer_size));  // DWORD DataLength
    if (buffer_id == RIO_INVALID_BUFFERID) {
        std::cout << "RIORegisterBuffer Error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Setup descriptor for address
    RIO_BUF remote_address {
        .BufferId = buffer_id,
        .Offset = 0,
        .Length = remote_address_length,
    };

    *reinterpret_cast<sockaddr_in*>(&buffer[0]) = {
        .sin_family = AF_INET,
        .sin_port = htons(UDP_DST_PORT),
        .sin_addr = {.S_un = {.S_addr = htonl(INADDR_LOOPBACK)}},
    };

    // Setup descriptors for data
    struct Descriptor {
        RIO_BUF rio_buf;
        char* buffer;
    };
    auto descriptors = new Descriptor[max_outstanding_requests] {};

    Packet packet {};
    for (size_t i = 0; i < max_outstanding_requests; i++) {
        descriptors[i].rio_buf = {
            .BufferId = buffer_id,
            // offset relative to start of buffer
            .Offset = static_cast<DWORD>(remote_address_length + i * max_packet_length),
            .Length = max_packet_length,
        };

        descriptors[i].buffer = &buffer[remote_address_length + i * max_packet_length];

        // Fill in payload
        packet.number = i;
        memcpy(descriptors[i].buffer, &packet, sizeof(packet));
        descriptors[i].rio_buf.Length = sizeof(packet);
    }

    // Enqueue descriptors
    // https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_riosendex
    for (size_t i = 0; i < max_outstanding_requests; i++) {
        if (int result = rio_extension_function_table.RIOSendEx(
                request_queue,            // RIO_RQ     SocketQueue,
                &descriptors[i].rio_buf,  // PRIO_BUF   pData,
                1,                        // ULONG      DataBufferCount,
                nullptr,                  // PRIO_BUF   pLocalAddress,
                &remote_address,          // PRIO_BUF   pRemoteAddress,
                nullptr,                  // PRIO_BUF   pControlContext,
                nullptr,                  // PRIO_BUF   pFlags,
                0,                        // DWORD      Flags,
                &descriptors[i]);         // PVOID      RequestContext
            result != TRUE) {
            std::cout << "RIOSendEx (1) Error: " << WSAGetLastError() << std::endl;
            return 1;
        }
    }

    size_t statistics_bytes_transferred = 0;
    size_t statistics_packets_sent = 0;

    using wall_clock = std::chrono::steady_clock;
    auto statistics_time = wall_clock::now();

    // ready
    for (;;) {
        // Signal that we are ready to receive
        // https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_rionotify
        rio_extension_function_table.RIONotify(completion_queue);

        // Wait for something to happen
        if (WaitForSingleObject(notification_event, INFINITE) != WAIT_OBJECT_0) {
            auto last_error = GetLastError();
            std::cout << "WaitForSingleObject failed with error: " << last_error << std::endl;
            return 1;
        }

        // Dequeue results
        // https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_riodequeuecompletion
        RIORESULT rio_results[16];

        auto results_dequeued = rio_extension_function_table.RIODequeueCompletion(
            completion_queue,                             // RIO_CQ       CQ,
            rio_results,                                  // PRIORESULT   Array,
            static_cast<DWORD>(std::size(rio_results)));  // ULONG        ArraySize

        if (0 == results_dequeued) {
            std::cout << "RIODequeueCompletion: No events dequeued!\n"
                      << "This is unexpected with working notifications." << std::endl;
            continue;
        }

        if (RIO_CORRUPT_CQ == results_dequeued) {
            std::cout << "RIODequeueCompletion Error: " << WSAGetLastError() << std::endl;
            return 1;
        }

        // Parse results for statistics
#if 1
        for (size_t i = 0; i < results_dequeued; i++) {
            statistics_bytes_transferred += rio_results[i].BytesTransferred;
            statistics_packets_sent++;
        }

        auto now = wall_clock::now();

        using namespace std::literals::chrono_literals;

        if (now - statistics_time >= 1s) {
            auto diff_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - statistics_time);
            auto bit_rate = (8 * 1000.0 * statistics_bytes_transferred / diff_time_ms.count());
            auto packet_rate = (1000.0 * statistics_packets_sent / diff_time_ms.count());
            std::cout << "Sent " << statistics_bytes_transferred << " bytes (" << statistics_packets_sent
                      << " packets) in " << diff_time_ms;
            std::cout << "  => " << packet_rate << " pkt/s or " << bit_rate << " bit/s" << std::endl;

            // next cycle
            statistics_time = now;
            statistics_bytes_transferred = 0;
            statistics_packets_sent = 0;
        }
#endif

        // Reuse buffers
        for (size_t i = 0; i < results_dequeued; i++) {
            // RequextContext was set to the descriptor in previous RIOSendEx call
            auto descriptor = reinterpret_cast<Descriptor*>(rio_results[i].RequestContext);

            packet.number++;
            memcpy(descriptor->buffer, &packet, sizeof(packet));
            descriptor->rio_buf.Length = sizeof(packet);

            // Enqueue
            // https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_riosendex
            if (int result = rio_extension_function_table.RIOSendEx(
                    request_queue,         // RIO_RQ     SocketQueue,
                    &descriptor->rio_buf,  // PRIO_BUF   pData,
                    1,                     // ULONG      DataBufferCount,
                    nullptr,               // PRIO_BUF   pLocalAddress,
                    &remote_address,       // PRIO_BUF   pRemoteAddress,
                    nullptr,               // PRIO_BUF   pControlContext,
                    nullptr,               // PRIO_BUF   pFlags,
                    0,                     // DWORD      Flags,
                    descriptor);           // PVOID      RequestContext
                result != TRUE) {
                std::cout << "RIOSendEx (2) Error: " << WSAGetLastError() << std::endl;
                return 1;
            }
        }
    }

    return 0;
}
