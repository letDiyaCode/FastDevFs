#include "../include/dedup_server.h"
#include "../include/dedup_ipc.h"
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

void dedup_worker() {
    int server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        std::cerr << "Dedup thread: Failed to create socket\n";
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DEDUP_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(DEDUP_SOCKET_PATH);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Dedup thread: Failed to bind socket\n";
        close(server_fd);
        return;
    }

    std::cout << "[Dedup Server] Thread started and listening on " << DEDUP_SOCKET_PATH << std::endl;

    while (true) {
        DedupRequest req;
        ssize_t n = recvfrom(server_fd, &req, sizeof(req), 0, nullptr, nullptr);
        if (n == sizeof(DedupRequest)) {
            std::cout << "[Dedup Thread] Working... Received event for file insertion/update!" << std::endl;
            std::cout << "[Dedup Thread] Operation: " << req.operation_type << ", Inode: " << req.inode << ", Path: " << req.path << std::endl;
        } else if (n > 0) {
            std::cerr << "[Dedup Thread] Received incomplete data\n";
        }
    }

    close(server_fd);
}

void start_dedup_server() {
    std::thread worker(dedup_worker);
    worker.detach();
}
