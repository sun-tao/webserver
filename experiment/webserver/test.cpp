#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>
#include "lst_timer.h"

#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define TIMESLOT 5

static int pipefd[2];
static sort_timer_lst timer_lst;  //自动执行默认构造初始化
static int epollfd = 0;

int setnoblocking(int fd){
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}
void addfd(int epollfd,int fd){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnoblocking(fd);
    return;
}

void sig_handler(int sig){
    //信号处理函数,收到信号后将其通过管道发送给主线程,主线程采用epoll监听
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1],(char*)&msg,1,0);
    errno = save_errno;
}

void addsig(int sig){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL) != -1);
    return;
}

void cb_func(client_data* user_data){
    // 定时到了之后的回调函数
    epoll_ctl(epollfd,EPOLL_CTL_DEL,user_data->sockfd,nullptr);
    close(user_data->sockfd);
    printf( "close fd %d\n", user_data->sockfd );
    return;
}

void timer_handler()
{
    timer_lst.tick(); // 查询定时器链表中是否有超时的定时器
    alarm( TIMESLOT );
}

int main(int argc , char* argv[]){
    if (argc <= 2){
        printf("usage: %s ip_address port_number\n", basename( argv[0] ));
        return 1;
    }

    const char* ip = argv[1];  //argv[0]代表执行命令
    int port = atoi(argv[2]);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET,SOCK_STREAM,0);
    assert(listen >= 0);
    //端口复用
    int reuse = 1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    ret = bind(listenfd,(struct sockaddr*) &address,sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd,5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd,listenfd);

    ret = socketpair(PF_UNIX,SOCK_STREAM,0,pipefd);
    assert(ret != -1);

    setnoblocking(pipefd[1]);//写端非阻塞
    addfd(epollfd,pipefd[0]); //epoll监听读端管道,统一事件源

    addsig(SIGALRM); // 设置当前进程特殊处理的信号
    addsig(SIGTERM);
    bool stop_server = false;
    bool timeout = false;
    client_data* users = new client_data[FD_LIMIT];
    alarm(TIMESLOT);

    while(!stop_server){
        int nums = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        for (int i = 0 ; i < nums ; i++){
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd){
                //有新用户连接
                printf("new user!\n");
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(sockfd,(struct sockaddr*)&client_address,&client_addrlength);
                addfd(epollfd,connfd);
                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;
                util_timer* timer = new util_timer; //新建计时器对象
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(nullptr);
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_lst.add_timer(timer); //升序链表中添加定时器
            }
            else if (sockfd == pipefd[0] && events[i].events & EPOLLIN){  
                //管道数据进来,即定时信号SIGALRM到了，需要对升序链表中的定时器
                //状态进行一一查询，查找到期的定时器
                char signals[1024];
                ret = recv(pipefd[0],signals,sizeof(signals),0);
                if (ret == -1){
                    continue;
                }
                else if (ret == 0){
                    continue;
                }
                else{
                    for (int j = 0 ; j < ret ; j++){
                        switch(signals[j]){
                            case SIGALRM :
                            {
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                                break;
                            }
                            default:{
                                break;
                            }          
                        }
                    }
                }
            }
            else if (events[i].events & EPOLLIN){
                memset(users[sockfd].buf,'\0',sizeof(users[sockfd].buf));
                ret = recv(sockfd,users[sockfd].buf,sizeof(users[sockfd].buf)-1,0);
                //printf( "get %d bytes of client data %s from %d\n", ret, users[sockfd].buf, sockfd );
                util_timer* timer = users[sockfd].timer;
                if (ret < 0){
                    printf( "ret<0\n" );
                    if (errno != EAGAIN){
                        cb_func(&users[sockfd]);
                        if (timer){
                            timer_lst.del_timer(timer); //缺析构
                        }
                    }
                }
                else if (ret == 0){
                    printf( "ret=0\n" );
                    //对方关闭连接
                    cb_func(&users[sockfd]);
                        if (timer){
                            timer_lst.del_timer(timer); //缺析构
                        }
                }
                else { 
                    if (timer){
                        time_t cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        printf( "adjust timer once\n" );
                        timer_lst.adjust_timer(timer);
                    }
                }
            }
            else{
                //others
            }
        }
        if (timeout){
            //printf("Timeout!");
            timer_handler();
            timeout = false;
        }
    }

    close(epollfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete[] users;
    return 0;
}