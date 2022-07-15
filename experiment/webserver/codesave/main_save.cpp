#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "http_conn.h"
#include "threadpool.h"
#include <signal.h>
#include <assert.h>
using namespace std;

#define MAX_EVENT_NUMBER 10000  //最多能处理的事件数量
#define MAX_FD 1000   //最多连接的用户数量
#define TIMESLOT 5

static int pipefd[2];
static sort_timer_lst timer_lst;

extern void addfd(int epollfd,int fd,bool oneshot);

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

void time_handler(){
    timer_lst.tick();
    alarm(TIMESLOT);
    return;
}

// void func(http_conn* user_data){
//     user_data->closefd();
//     return;
// }

int main(int argc , char* argv[]){
    if (argc <= 1){  //任务数只有1个，则未带端口号，输入不正确
        cout << "请输入端口号" << endl;
        exit(-1);
    }
    int port = atoi(argv[1]);
    
    //需要用到的资源
    Threadpool<http_conn>* pool = NULL; 
    try{
        pool = new Threadpool<http_conn>;
    }
    catch(...){
        cout << "创建线程池失败" << endl;
        return -1; 
    }

    int listenfd = socket(PF_INET,SOCK_STREAM,0);  //创建socket

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;   //IP地址
    address.sin_family = AF_INET;  //协议IPV4
    address.sin_port = htons(port);  //端口号

    //端口复用
    int reuse = 1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    ret = bind(listenfd,(struct sockaddr*) &address, sizeof(address));
    ret = listen(listenfd,5);

    http_conn* users = new http_conn[MAX_FD]; // 创建管理客户端连接的http_conn数组，可以管理多个客户端的连接,一个客户端连接即为一个http_conn
    epoll_event events[MAX_EVENT_NUMBER]; 
    int epollfd = epoll_create(2000); // 创建epoll对象

    http_conn::epollfd = epollfd; 
    http_conn::user_num = 0;
    
    addfd(epollfd,listenfd,false);
    addfd(epollfd,pipefd[0],false);

    addsig(SIGTERM);
    addsig(SIGALRM);
    alarm(TIMESLOT);

    bool timeout = false;
    printf("start!\n");
    while(true){
        int nums = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1); 
        printf("go!");
        if (nums < 0 && errno != EINTR){
            cout << "epoil fail!" << endl;
            break;
        }
        
        for (int i = 0 ; i < nums ; i++){ //i,处理单个文件描述符和其对应事件
            int socketfd = events[i].data.fd;
            if (socketfd == listenfd){
                printf("new user!\n");
                struct sockaddr_in client_address;
                socklen_t client_address_len = sizeof(client_address);
                int connfd = accept(listenfd,(struct sockaddr*)& client_address , &client_address_len);
                if (http_conn::user_num > MAX_FD){
                    cout << "连接的用户数量过多！" << endl;
                    close(connfd);
                    continue;
                }
                if (connfd < 0){
                    perror("accept!");
                    continue;
                }
                users[connfd].init(connfd,client_address);
                timer_lst.add_timer(users[connfd].timer);
                continue;
            }
            else if(events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)){  // 这里不清楚具体含义，只知道是出现问题了
                users[socketfd].closefd();
            }
            else if(socketfd == pipefd[0] && events[i].events & EPOLLIN){
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
                            default:{
                                break;
                            }          
                        }
                    }
                }
            }
            else if(events[i].events & EPOLLIN){  // 读事件
                //循环读，将缓冲区的内容读空
                if (users[socketfd].mread()){ //正确读完
                    users[socketfd].timer->expire = time(nullptr) + 3 * TIMESLOT; //更新定时器时间
                    printf("adjust timer once");
                    timer_lst.adjust_timer(users[socketfd].timer);
                    pool->appendRequest(&users[socketfd]);
                }
                else{ //读失败了
                    users[socketfd].closefd();
                    timer_lst.del_timer(users[socketfd].timer);
                }
            }
            else if (events[i].events & EPOLLOUT){
                //循环写
                //cout << "可以开始写啦!" << endl;
                if (!users[socketfd].mwrite()){
                    users[socketfd].closefd();
                    timer_lst.del_timer(users[socketfd].timer);
                }
                else{ 
                    users[socketfd].timer->expire = time(nullptr) + 3 * TIMESLOT; //更新定时器时间
                    printf("adjust timer write!");
                    timer_lst.adjust_timer(users[socketfd].timer);
                }
            }

            if (timeout){
                time_handler();
                timeout = false;
            }
        }
    }

    //释放资源
    delete pool;
    delete[] users;
    close(listenfd);
    close(epollfd);
    return 0;
}