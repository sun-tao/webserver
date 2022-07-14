#ifndef LST_TIMER
#define LST_TIMER
#include <arpa/inet.h>
#include <stdio.h>
#include <time.h>
#define BUFFER_SIZE 1024

//实现了自制的升序双向链表，链表内存储定时器
class util_timer; // 前置申明
class client_data{
public:
    int sockfd;
    struct sockaddr_in address; //客户端socket地址
    char buf[BUFFER_SIZE];
    util_timer* timer;  
};

class util_timer{
public:
    util_timer():prev(nullptr),next(nullptr){}

public:
    time_t expire; //设定的任务超时时间
    void (*cb_func) (client_data*); //超时后触发任务的回调函数，用来删除用户数据的
    client_data* user_data;
    util_timer* prev;
    util_timer* next;
};

class sort_timer_lst{
public:
    // sort_timer_lst& operator=(sort_timer_lst timer_lst){

    // }
    sort_timer_lst():head(nullptr),tail(nullptr){}
    ~sort_timer_lst(){
        util_timer* tmp = head;
        while(tmp != nullptr){
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    void add_timer(util_timer* timer){
        if (timer == nullptr){
            return;
        }
        if (head == nullptr && tail == nullptr){
            head = timer;
            tail = timer;
            return;
        }
        //assert(tail != nullptr);
        if (timer->expire < head->expire){
            //插入到头结点之前
            timer->prev = nullptr;
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        if (timer->expire > tail->expire){
            //插入到尾结点之后
            tail->next = timer;
            timer->next = nullptr;
            timer->prev = tail;
            tail = timer;
            return;
        }
        util_timer* tmp = head;
        while (tmp != nullptr){
            if (timer->expire < tmp->expire){
                //1.非头结点,非尾结点
                util_timer* tmp_prior = tmp->prev;
                timer->next = tmp_prior->next;
                tmp->prev = timer;
                tmp_prior->next = timer;
                timer->prev = tmp_prior;
                break;
            }
            tmp = tmp->next;
        }
        return;
    }

    void del_timer(util_timer* timer){
        if (timer == head && timer == tail){
            // printf("1\n");
            delete timer;
            head = nullptr;
            tail = nullptr;
            return;
        }

        if (timer == head){
            // printf("2\n");
            head = head->next;
            head->prev = nullptr;
            delete timer;
            return;
        }

        if (timer == tail){
            // printf("3\n");
            tail = tail->prev;
            tail->next = nullptr;
            delete timer;
            return;
        }

        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
        return;
    }

    void adjust_timer(util_timer* timer){
        //将更新的timer重新插入双向升序链表中
        if (timer == nullptr){
            return;
        }
        if (head == tail){ //双向链表中仅有一个元素
            head = nullptr;
            tail = nullptr;
        }
        if (timer == head){
            head = head->next;
            head->prev = nullptr;
        }
        else if (timer == tail){
            tail = tail->prev;
            tail->next = nullptr;
        }
        else{
            util_timer* tmp = head; //找到待更新的结点
            while(tmp != nullptr){
                if (tmp == timer){
                    //从原链表中删去该结点
                    tmp->prev->next = tmp->next;
                    tmp->next->prev = tmp->prev;
                    break;
                }
                tmp = tmp->next;
            }
        }
        add_timer(timer);
        return;
    }

    void tick(){ //查询定时器链表,检查是否有到时的定时器
        //大坑，在这边明明可以直接删除超时的定时器的，非得调用delete，而head在两个函数直接穿梭
        //导致出现很多莫名其妙的错误！！！！！！！
        time_t now = time(nullptr);
        util_timer* tmp = head;
        while(tmp != nullptr){
            if (now < tmp->expire){
                break;
            }
            //超时
            printf("tick1!\n");
            tmp->cb_func(tmp->user_data);//删除对应用户
            head = tmp->next;  //head在此发生了变化,不能再调用del_timer了，不然大概率出错
            if (head){
                head->prev = nullptr;
            }
            else{
                tail = nullptr;
            }
            delete tmp;
            tmp = head;
        }
        //showElement();
        return;
    }

    void showElement(){
        util_timer* tmp = head;
        while(tmp != nullptr){
            printf("%d\n",(int)tmp->expire);
            tmp = tmp->next;
        }
        return;
    }

public:
    util_timer* head;
    util_timer* tail;
};

#endif