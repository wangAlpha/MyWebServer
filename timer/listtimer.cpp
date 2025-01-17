

#include "listtimer.h"

ListTimer::ListTimer() : head_(nullptr), tail_(nullptr) {}

ListTimer::~ListTimer() {
    Timer *node = head_;
    while (node) {
        head_ = node->next_;
        delete node;
        node = head_;
    }
}

void ListTimer::AddTimer(Timer *timer) {
    if (timer == nullptr) return;

    if (head_ == nullptr) {
        head_ = tail_ = timer;
        return;
    }

    // 如果新的定时器超时时间小于当前头部结点, 直接将当前定时器结点作为头部结点
    if (timer->expire_ < head_->expire_) {
        timer->next_ = head_;
        head_->prev_ = timer;
        head_ = timer;
        return;
    }

    AddTimer(timer, head_);  // 否则调用私有成员，调整内部结点
}

// TODO(blogchen@qq.com): 由于该项目的特殊性，这里可以直接尾插
void ListTimer::AddTimer(Timer *timer, Timer *last_head) {
    Timer *prev = last_head;
    Timer *tmp = prev->next_;

    // 遍历当前结点之后的链表，按照超时时间找到目标定时器对应的位置，常规双向链表插入操作
    while (tmp) {
        if (timer->expire_ < tmp->expire_) {
            prev->next_ = timer;
            timer->next_ = tmp;
            tmp->prev_ = timer;
            timer->prev_ = prev;
            break;  // 可以直接 return
        }
        prev = tmp;
        tmp = tmp->next_;
    }

    // 遍历完未找到位置，目标定时器需要放到尾结点处
    if (tmp == nullptr) {
        prev->next_ = timer;
        timer->prev_ = prev;
        timer->next_ = nullptr;
        tail_ = timer;
    }
}

void ListTimer::AdjustTimer(Timer *timer) {
    if (timer == nullptr) return;

    Timer *tmp = timer->next_;

    /*  1. 被调整的定时器在链表尾部
        2. 定时器超时值仍然小于下一个定时器超时值，不调整 */
    if (tmp == nullptr || (timer->expire_ < tmp->expire_)) return;

    // 被调整定时器是链表头结点，将定时器取出，重新插入
    if (timer == head_) {
        head_ = head_->next_;
        head_->prev_ = nullptr;
        timer->next_ = nullptr;
        AddTimer(timer, head_);
    }

    // 被调整定时器在内部，将定时器取出，重新插入
    else {  // timer != head_
        timer->prev_->next_ = timer->next_;
        timer->next_->prev_ = timer->prev_;
        AddTimer(timer, timer->next_);
    }
}

void ListTimer::DelTimer(Timer *timer) {
    if (timer == nullptr) return;

    // 链表中只有一个定时器，需要删除该定时器
    if ((timer == head_) && (timer == tail_)) {
        delete timer;
        head_ = nullptr;
        tail_ = nullptr;
        return;
    }

    // 被删除的定时器为头结点
    if (timer == head_) {
        head_ = head_->next_;
        head_->prev_ = nullptr;
        delete timer;
        return;
    }

    // 被删除的定时器为尾结点
    if (timer == tail_) {
        tail_ = tail_->prev_;
        tail_->next_ = nullptr;
        delete timer;
        return;
    }

    // 被删除的定时器在链表内部，常规链表结点删除
    timer->prev_->next_ = timer->next_;
    timer->next_->prev_ = timer->prev_;
    delete timer;
}

// 由SIGALARM信号触发, 处理链表上到期任务。
void ListTimer::Tick() {
    if (head_ == nullptr) return;
    time_t cur = time(nullptr);  // 获取当前系统时间
    Timer *tmp = head_;
    // 从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
    while (tmp) {
        /* 因为每个定时器都使用绝对时间作为超时值，所以可以把定时器的超时值和系统当前时间，
        比较以判断定时器是否到期*/
        if (cur < tmp->expire_) break;

        // 调用定时器的回调函数，以执行定时任务
        tmp->CallbackFunction(tmp->user_data_);
        LOG_INFO("Client disconnect : conn_fd = %d, client_address = %s",
                 tmp->user_data_->sock_fd,
                 inet_ntoa(tmp->user_data_->address.sin_addr));

        // 执行完定时器中的定时任务之后，就将它从链表中删除，并重置链表头节点
        head_ = tmp->next_;
        if (head_) head_->prev_ = nullptr;

        delete tmp;
        tmp = head_;
    }
}

void TimerUtils::Init(int time_slot) {
    time_slot_ = time_slot;
    return;
}

// 对文件描述符设置非阻塞
int TimerUtils::SetNonBlocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void TimerUtils::AddFd(int epoll_fd, int fd, bool one_shot, int trigger_mode) {
    epoll_event event;
    event.data.fd = fd;

    if (trigger_mode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot) event.events |= EPOLLONESHOT;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    SetNonBlocking(fd);
}

// 信号处理函数
void TimerUtils::SignalHandler(int sig) {
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipe_fd_[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号函数
void TimerUtils::AddSignal(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void TimerUtils::TimerHandler() {
    timer_list_.Tick();
    alarm(time_slot_);
}

void TimerUtils::ShowError(int conn_fd, const char *info) {
    send(conn_fd, info, strlen(info), 0);
    close(conn_fd);
}

int *TimerUtils::pipe_fd_ = 0;
int TimerUtils::epoll_fd_ = 0;

class TimerUtils;
void CallbackFunction(ClientData *user_data) {
    epoll_ctl(TimerUtils::epoll_fd_, EPOLL_CTL_DEL, user_data->sock_fd, 0);
    assert(user_data);
    close(user_data->sock_fd);
    HttpConn::user_count_--;
}