#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <cstring>
#include <cerrno>

#define MAX_EVENTS 1024

// 将文件描述符设置为非阻塞
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));//服务器关闭后立即重启，无需等待time_wait状态结束

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    bind(listen_fd, (struct sockaddr*)&address, sizeof(address));
    listen(listen_fd, SOMAXCONN);

    // 1. 创建 epoll 实例
    int epoll_fd = epoll_create1(0);

    // 2. 将 listen_fd 添加到 epoll 中
    epoll_event event;
    event.data.fd = listen_fd;
    // EPOLLIN: 可读事件 | EPOLLET: 边缘触发模式
    event.events = EPOLLIN | EPOLLET; 
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event);//如果有新连接,就会把listen_fd加到event中去

    // 为了使用 ET 模式，listen_fd 也必须是非阻塞的
    set_nonblocking(listen_fd);

    std::cout << "Epoll Server listening on port 8080 (ET mode)...\n";

    epoll_event events[MAX_EVENTS];//所有就绪的事件都在这里方便系统调用

    while (true) {
        // 3. 阻塞等待事件发生
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);//告诉wait监听event里的事件，状态改变就返回

        for (int i = 0; i < num_events; i++) {
            int current_fd = events[i].data.fd;

            if (current_fd == listen_fd) {
                // 【处理新连接】
                // 因为是 ET 模式，可能同时到达多个连接，必须用 while 循环全部 accept 出来
                while (true) {
                    sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len);
                    
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // 所有新连接都处理完了
                        } else {
                            continue; // 其他错误
                        }
                    }

                    // 将新客户端 fd 设为非阻塞并加入 epoll (ET 模式)
                    set_nonblocking(client_fd);
                    event.data.fd = client_fd;
                    event.events = EPOLLIN | EPOLLET;//可读与边缘触发
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);//将新连接加入红黑树中
                }
            } 
            else if (events[i].events & EPOLLIN) {
                // 【处理客户端发来的数据】
                char buffer[1024];
                
                // 关键学习点：ET 模式下的 while(read)
                while (true) {
                    ssize_t bytes_read = read(current_fd, buffer, sizeof(buffer));
                    
                    if (bytes_read > 0) {
                        // 读取到数据（此处为了简化，直接将数据原样返回，实际应解析 HTTP）
                        write(current_fd, buffer, bytes_read);
                    } 
                    else if (bytes_read == 0) {
                        // 客户端正常关闭连接
                        close(current_fd); // close 会自动将 fd 从 epoll 中移除
                        break;
                    } 
                    else if (bytes_read == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // 内核读取缓冲区已经被读空了，退出循环，等待下一次 epoll 通知
                            break;
                        } else {
                            // 发生真正的错误
                            close(current_fd);
                            break;
                        }
                    }
                }
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);
    return 0;
}
