#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

int main() {
    // 1. 创建 socket (IPv4, TCP)
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation failed\n";
        return -1;
    }

    // 设置端口复用，防止重启时 Address already in use
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 监听所有本地网卡
    address.sin_port = htons(8080);       // 监听 8080 端口，需转换为网络字节序

    // 2. 绑定端口和IP
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        return -1;
    }

    // 3. 开始监听，128 是全连接队列的最大长度
    if (listen(server_fd, 128) < 0) {
        std::cerr << "Listen failed\n";
        return -1;
    }

    std::cout << "Server is listening on port 8080...\n";

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        // 4. 阻塞点 1：等待客户端连接
        // 如果没有客户端连接，当前线程会一直挂起在这里
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            std::cerr << "Accept failed\n";
            continue;
        }

        std::cout << "Accepted new connection.\n";

        char buffer[1024] = {0};
        
        // 5. 阻塞点 2：读取客户端数据
        // 如果客户端建立了连接但迟迟不发送数据，线程会卡死在这里，导致后面的 accept 无法执行
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        
        if (bytes_read > 0) {
            std::cout << "Received request:\n" << buffer << std::endl;

            // 6. 写入响应
            // 构造一个最简单的 HTTP 响应报文
            const char* http_response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 12\r\n"
                "Content-Type: text/plain\r\n"
                "\r\n"
                "Hello World!";
            
            write(client_fd, http_response, strlen(http_response));
        }

        // 7. 关闭客户端连接
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
