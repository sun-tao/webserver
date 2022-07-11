#ifndef LOCKER_H
#define LOCKER_H
#include <exception>
#include <pthread.h>
#include <semaphore.h>
#include <iostream>

using namespace std;
//线程同步机制封装
//其实就是对一些现成的多线程api做了封装，免去每次书写很长的函数名

class Locker{
public:
    Locker(){
        if(pthread_mutex_init(&mutex,NULL) != 0){ //互斥锁初始化
            throw exception();
        }; 
    }
    ~Locker(){
        pthread_mutex_destroy(&mutex); //销毁互斥锁
    }

    bool lock(){
        int ret = pthread_mutex_lock(&mutex); // 互斥锁加锁
        if (ret == 0){  // 成功
            return true;
        }
        return false;
    }

    bool unlock(){
        int ret = pthread_mutex_unlock(&mutex); //解锁
        if (ret == 0){
            return true;
        }
        return false;
    }

    pthread_mutex_t * get(){
        return &mutex;
    }
private:
    pthread_mutex_t mutex;
};

//条件变量类
class Cond{
public:
    Cond(){
        int ret = pthread_cond_init(&cond,NULL);  //条件变量初始化
        if (ret != 0){
            throw exception();
        }
    }
    ~Cond(){
        int ret = pthread_cond_destroy(&cond);
        if (ret != 0){
            cout << "Cond析构失败！" << endl;
            exit(-1);
        }
    }

    bool wait(pthread_mutex_t* mutex){
        int ret = pthread_cond_wait(&cond,mutex);
        if (ret != 0){
            return false;
        }
        return true;
    }

    bool timewait(pthread_mutex_t* mutex,struct timespec t){
        int ret = pthread_cond_timedwait(&cond,mutex,&t);
        if (ret != 0){
            return false;
        }
        return true;
    }
private:
    pthread_cond_t cond;
};

class Sem{
public:
    Sem(){  // 默认构造函数，初始化的容量为0
        int ret = sem_init(&sem,0,0);
        if (ret != 0){
            throw exception();
        }
    }
    Sem(int num){  // 默认构造函数，初始化的容量为num
        int ret = sem_init(&sem,0,num);
        if (ret != 0){
            throw exception();
        }
    }
    ~Sem(){
        int ret = sem_destroy(&sem);
        if (ret != 0){
            cout << "Sem 析构失败!" << endl;
            exit(-1);
        }
    }
    bool wait(){ //减少信号量容量，若为0则阻塞
        int ret = sem_wait(&sem);
        if (ret != 0){
            return false;
        }
        return true;
    }
    bool post(){ //增加信号量容量
        int ret = sem_post(&sem);
        if (ret != 0){
            return false;
        }
        return true;
    }

private:
    sem_t sem;
};

#endif