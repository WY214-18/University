#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <thread>

// 独立处理每个客户端连接的函数
void handle_client(int client_fd) {
    char buffer[1024] = {0};
    
    // 这里依然是阻塞 IO，但它现在只阻塞当前的子线程，不影响主线程
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        std::cout << "Thread ID " << std::this_thread::get_id() << " received data.\n";
        
        const char* http_response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 20\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "Hello from thread!";
        
        write(client_fd, http_response, strlen(http_response));
    }

    close(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 128);

    std::cout << "Multithreaded Server listening on port 8080...\n";

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        // 主线程只负责 accept
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            continue;
        }

        // 开辟新线程处理该连接
        std::thread t(handle_client, client_fd);
        
        // 【关键点】：必须分离线程 (detach)。
        // 如果调用 t.join()，主线程又会阻塞等待子线程结束，退化成同步阻塞模型。
        // 如果什么都不调用，std::thread 对象析构时会触发 std::terminate 导致程序崩溃。
        t.detach(); 
    }

    close(server_fd);
    return 0;
}
