#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <list> 
#include <iostream>
#include <semaphore.h>

using namespace std;
pthread_mutex_t mutex;
pthread_cond_t cond;


list<int> goods;

void * producer(void * arg){
    while(1){
        //生产者
        pthread_mutex_lock(&mutex);
        int good = rand() % 10;
        goods.push_back(good);
        std::cout << "produce good id = " << good << std::endl;
        usleep(100);
        //发送信号，通知消费者消费
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
    }
}

void * customer(void * arg){
    while(1){
        pthread_mutex_lock(&mutex);
        if (goods.size() != 0){
            int good = goods.front();
            goods.pop_front();
            cout << "consume good id= " << 0 << endl;
            usleep(100); 
            pthread_mutex_unlock(&mutex);
        }
        else{
            //此函数会阻塞，当其阻塞时，将互斥锁解锁，便于其他线程对其访问，当其解除阻塞时，对互斥锁加锁
            //挂起，不占用cpu资源
            pthread_cond_wait(&cond,&mutex);
            pthread_mutex_unlock(&mutex);
        }
    }
}
int main(){
    
    pthread_mutex_init(&mutex,NULL);
    pthread_cond_init(&cond,NULL);

    pthread_t  ptids[5],ctids[5];
    for (int i = 0 ; i < 5; i++){
        pthread_create(&ptids[i],NULL,producer,NULL);
        pthread_create(&ctids[i],NULL,customer,NULL);
    }

    for (int i = 0 ; i < 5 ; i++){
        pthread_detach(ptids[i]);
        pthread_detach(ctids[i]);
    }

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    pthread_exit(NULL);
}
