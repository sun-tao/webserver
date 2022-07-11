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

sem_t psem;
sem_t csem;
list<int> goods;

void * producer(void * arg){
    int cnt = 10;
    while(1){
        //生产者
        sem_wait(&psem);
        pthread_mutex_lock(&mutex);
        int good = rand() % 10;
        goods.push_back(good);
        std::cout << "produce good id = " << good << std::endl;
        usleep(100);
        //发送信号，通知消费者消费
        pthread_mutex_unlock(&mutex);
        sem_post(&csem);
        usleep(100);
    }
}

void * customer(void * arg){
    while(1){
        sem_wait(&csem);
        pthread_mutex_lock(&mutex);
        int good = goods.front();
        goods.pop_front();
        cout << "consume good id= " << good << endl;
        usleep(100); 
        pthread_mutex_unlock(&mutex);
        sem_post(&psem);
        usleep(100);
    }
}
int main(){
    
    pthread_mutex_init(&mutex,NULL);
    
    sem_init(&psem,0,8);
    sem_init(&csem,0,0);
    pthread_t  ptids[5],ctids[5];
    for (int i = 0 ; i < 5; i++){
        pthread_create(&ptids[i],NULL,producer,NULL);
        pthread_create(&ctids[i],NULL,customer,NULL);
    }

    for (int i = 0 ; i < 5 ; i++){
        pthread_detach(ptids[i]);
        pthread_detach(ctids[i]);
    }

    while(1){
        sleep(10);
    }

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    pthread_exit(NULL);
}