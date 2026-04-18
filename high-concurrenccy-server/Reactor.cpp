#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <vector>
#include <functional>
#include <cstring>
#include <fcntl.h>

class EventLoop; // 前向声明

// ==========================================
// 1. Channel 类：负责保管 fd 和回调函数
// ==========================================
class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd), events_(0), revents_(0) {}

    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    
    // 核心：当 epoll 发现这个 fd 有动静时，会调用这个函数
    void handleEvent() {
        if (revents_ & EPOLLIN) {
            if (readCallback_) readCallback_();
        }
    }

    void enableReading(); // 具体实现在 EventLoop 定义之后

    int fd() const { return fd_; }
    void setRevents(uint32_t revt) { revents_ = revt; }
    uint32_t events() const { return events_; }

private:
    EventLoop* loop_;
    int fd_;
    uint32_t events_;  
    uint32_t revents_; 
    EventCallback readCallback_;
};

// ==========================================
// 2. Poller 类：纯粹封装 epoll
// ==========================================
class Poller {
public:
    Poller() { epollfd_ = epoll_create1(0); }
    ~Poller() { close(epollfd_); }

    // 阻塞等待，返回有事件发生的 Channel
    void poll(std::vector<Channel*>* activeChannels) {
        int numEvents = epoll_wait(epollfd_, events_, 1024, -1);
        for (int i = 0; i < numEvents; ++i) {
            // epoll_event 里的指针强转回 Channel*
            Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
            channel->setRevents(events_[i].events);
            activeChannels->push_back(channel);
        }
    }

    // 往内核 epoll 红黑树里添加/修改事件
    void updateChannel(Channel* channel) {
        struct epoll_event ev;
        ev.events = channel->events();
        ev.data.ptr = channel; // 关键：把 Channel 对象的地址存入内核！
        epoll_ctl(epollfd_, EPOLL_CTL_ADD, channel->fd(), &ev);/加入到红黑树中进行监听
    }

private:
    int epollfd_;
    struct epoll_event events_[1024];
};

// ==========================================
// 3. EventLoop 类：心脏，驱动整个循环
// ==========================================
class EventLoop {
public:
    EventLoop() : poller_(new Poller()) {}
    ~EventLoop() { delete poller_; }

    void loop() {
        std::cout << "EventLoop started." << std::endl;
        while (!quit_) {
            activeChannels_.clear();
            poller_->poll(&activeChannels_); // 1. 阻塞等事件
            
            for (Channel* channel : activeChannels_) {
                channel->handleEvent();      // 2. 触发回调，所谓回调就是提前写好的执行等到调用再回头执行
            }
        }
    }

    void updateChannel(Channel* channel) {
        poller_->updateChannel(channel);
    }

private:
    bool quit_ = false;
    Poller* poller_;
    std::vector<Channel*> activeChannels_;
};

// 补充 Channel 的 enableReading 实现 (因为用到了 loop_->updateChannel)
void Channel::enableReading() {
    events_ |= EPOLLIN;
    loop_->updateChannel(this);
}

// 设置非阻塞辅助函数
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ==========================================
// 4. main 函数：将所有组件组装起来
// ==========================================
int main() {
    // 1. 创建并绑定 Listen Socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    
    bind(listen_fd, (struct sockaddr*)&address, sizeof(address));
    listen(listen_fd, SOMAXCONN);
    set_nonblocking(listen_fd);

    // 2. 创建 Reactor 心脏
    EventLoop loop;//这时候自动调用Eventloop和poll构造函数

    // 3. 为 listen_fd 创建 Channel 
    Channel* acceptChannel = new Channel(&loop, listen_fd);//自动调用Channel构造函数

    // 4. 定义如果有新连接到来，该怎么做？(利用 Lambda 表达式做回调)
    acceptChannel->setReadCallback([&loop, listen_fd]() {//注意没有调用,意思只是将lamba赋值给readrollback但现在不执行，后面readcallback才执行
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        
        if (client_fd > 0) {
            std::cout << "New connection accepted! fd: " << client_fd << std::endl;
            set_nonblocking(client_fd);

            // 为新客户端创建一个 Channel
            Channel* clientChannel = new Channel(&loop, client_fd);
            
            // 定义如果客户端发来数据，该怎么做？
            clientChannel->setReadCallback([clientChannel]() {
                char buffer[1024] = {0};
                ssize_t bytes_read = read(clientChannel->fd(), buffer, sizeof(buffer));
                
                if (bytes_read > 0) {
                    std::cout << "Received from fd " << clientChannel->fd() << ": " << buffer;
                    write(clientChannel->fd(), "HTTP/1.1 200 OK\r\n\r\nReactor Echo!", 33);
                } else if (bytes_read == 0) {
                    std::cout << "Client disconnected: " << clientChannel->fd() << std::endl;
                    close(clientChannel->fd());
                    // 简版内存泄漏忽略处理：真实项目中这里需要从内核摘除 fd 并 delete clientChannel
                }
            });
            
            // 启动对客户端可读事件的监听
            clientChannel->enableReading();
        }
    });

    // 5. 启动对 listen_fd 可读事件的监听
    acceptChannel->enableReading();//先后调用enableReading()，updateChannel

    // 6. 发动机点火，死循环开始
    loop.loop();

    return 0;
}
