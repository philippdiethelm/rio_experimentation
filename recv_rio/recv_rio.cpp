
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <winsock2.h>
#include <mswsock.h>
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
    auto sockfd = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_REGISTERED_IO);

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

    // Get RIO functions from API
    RIO_EXTENSION_FUNCTION_TABLE rio_extension_function_table {};
    GUID GUID_WSAID_MULTIPLE_RIO = WSAID_MULTIPLE_RIO;
    DWORD bytes_returned = 0;

    if (int result = WSAIoctl(
            sockfd,
            SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
            &GUID_WSAID_MULTIPLE_RIO,
            sizeof(GUID_WSAID_MULTIPLE_RIO),
            (void**)&rio_extension_function_table,
            sizeof(rio_extension_function_table),
            &bytes_returned,
            nullptr,
            nullptr);
        result != 0) {
        auto last_error = ::GetLastError();
        std::cout << "WSAIoctl Error: " << last_error << std::endl;
        return 1;
    }

    // Create event for completion
    auto notification_event = WSACreateEvent();
    if(notification_event == WSA_INVALID_EVENT) {
        std::cout << "WSACreateEvent Error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Setup completion queue
    RIO_NOTIFICATION_COMPLETION completion_spec {
        .Type = RIO_EVENT_COMPLETION,
        .Event = {
            .EventHandle = notification_event,
            .NotifyReset = TRUE,
        },
    };

    constexpr DWORD max_outstanding_receive = 128;

    auto completion_queue =
        rio_extension_function_table.RIOCreateCompletionQueue(max_outstanding_receive, &completion_spec);
    if (completion_queue == RIO_INVALID_CQ) {
        std::cout << "RIOCreateCompletionQueue Error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Setup request queue
    auto request_queue = rio_extension_function_table.RIOCreateRequestQueue(
        sockfd, max_outstanding_receive, 1, 0, 1, completion_queue, completion_queue, nullptr);
    if (request_queue == RIO_INVALID_RQ) {
        std::cout << "RIOCreateRequestQueue Error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Setup buffers
    constexpr size_t max_receive_length = 1024;
    constexpr size_t buffer_size = max_outstanding_receive * max_receive_length;

    char* buffer = reinterpret_cast<char*>(malloc(buffer_size));
    if (buffer == nullptr) {
        std::cout << "Error allocating buffer of size " << buffer_size << std::endl;
        return 1;
    }

    // Register buffer
    auto buffer_id = rio_extension_function_table.RIORegisterBuffer(buffer, static_cast<DWORD>(buffer_size));
    if (buffer_id == RIO_INVALID_BUFFERID) {
        std::cout << "RIORegisterBuffer Error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Setup descriptors
    auto receive_descriptors = new RIO_BUF[max_outstanding_receive];

    for (size_t i = 0; i < max_outstanding_receive; i++) {
        receive_descriptors[i].BufferId = buffer_id;
        receive_descriptors[i].Length = max_receive_length;
        receive_descriptors[i].Offset = i * max_receive_length; // offset realtive to start of buffer
    }

    // Enqueue descriptors
    for (size_t i = 0; i < max_outstanding_receive; i++) {
        if (int result = rio_extension_function_table.RIOReceive(
                request_queue,
                &receive_descriptors[i],
                1,
                0,
                &receive_descriptors[i] /* RequestContext for re-enqueue later*/);
            result != TRUE) {
            std::cout << "RIOReceive Error: " << WSAGetLastError() << std::endl;
            return 1;
        }
    }

    std::cout << "Ready to receive data on UDP port " << UDP_DST_PORT << std::endl;


    for (;;) {
        // Signal that we are ready to receive
        rio_extension_function_table.RIONotify(completion_queue);

        // Wait for something to happen
        if (WaitForSingleObject(notification_event, INFINITE) != WAIT_OBJECT_0) {
            auto last_error = GetLastError();
            std::cout << "WaitForSingleObject failed with error: " << last_error << std::endl;
            return 1;
        }

        // Dequeue results
        RIORESULT rio_results[16];

        auto results_dequeued =
            rio_extension_function_table.RIODequeueCompletion(completion_queue, rio_results, std::size(rio_results));

        if (0 == results_dequeued) {
            std::cout << "RIODequeueCompletion: No events dequeued!\n"
                      << "This is unexpected with working notifications." << std::endl;
            continue;
        }

        if (RIO_CORRUPT_CQ == results_dequeued) {
            std::cout << "RIODequeueCompletion Error: " << WSAGetLastError() << std::endl;
            return 1;
        }

        // Parse results
        for (size_t i = 0; i < results_dequeued; i++) {
            std::cout << "Received " << (i + 1) << " / " << results_dequeued << ": " << rio_results[i].BytesTransferred
                      << " Bytes" << std::endl;
        }

        // Reuse buffers
        for (size_t i = 0; i < results_dequeued; i++) {
            // RequextContext was set to buffer in previous RIOReceive call
            auto buffer = reinterpret_cast<RIO_BUF*>(rio_results[i].RequestContext);

           if (auto result = rio_extension_function_table.RIOReceive(
                request_queue, buffer, 1, 0, buffer /* RequestContext for re-enqueue next time*/);
                result != TRUE) {
                std::cout << "RIOReceive Error: " << WSAGetLastError() << std::endl;
                return 1;
            }
        }
    }

    return 0;
}
