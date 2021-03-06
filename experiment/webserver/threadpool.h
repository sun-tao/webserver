#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <list>
#include <pthread.h>
#include <mutex>
#include "locker.h"

using namespace std;
template<typename T>
class Threadpool{  // 模板类
public:
    Threadpool(int max_request_num = 1000 , int thread_num = 8);
    ~Threadpool();
    bool appendRequest(T* request);
private:
    //定义线程工作函数,开启线程后执行此函数
    static void* worker(void* arg);
    //定义线程业务逻辑执行函数，执行线程的业务逻辑
    void run();
private:
    int max_request_num;  //请求队列中的最大请求数
    list<T*> request_queue; //请求队列

    int thread_num ;  //工作线程数
    pthread_t* pths;   // 工作线程集合

    Locker queue_locker;  // 互斥锁对象,定义在自己写的头文件中
    Sem queue_stat ; //请求队列状态的信号量，同上

    bool stop ; //是否结束工作线程
};

template<typename T>
Threadpool<T>::Threadpool(int max_request_num  , int thread_num ):
               max_request_num(max_request_num),thread_num(thread_num),stop(false),pths(NULL)                     
{
    //初始化一些资源
    if (max_request_num< 0 || thread_num < 0){
        throw exception();
    }
    
    //创建工作线程
    pths = new pthread_t[thread_num];
    for (int i = 0 ; i <= thread_num ; i++){ 
        int ret = pthread_create(pths+i,NULL,worker,this);  //创建thread_num个工作线程
        if (ret != 0){
            delete[] pths;
            throw exception();
        }
        ret = pthread_detach(pths[i]);  //对每个创建的线程进行线程分离
        if (ret != 0){
            delete[] pths;
            throw exception();
        }
    }

    //这里也可以对互斥锁和信号量等成员变量进行初始化
    //但当类的对象定义的时候，其构造函数会自动对这两个量进行默认初始化，默认初始化等价于下面两行代码
    // queue_locker = Locker(); //构造互斥锁对象
    // queue_stat = Sem();
}

template<typename T>
Threadpool<T>::~Threadpool(){
    delete[] pths;
    stop = true;
}

template<typename T>
bool Threadpool<T>::appendRequest(T* request){
    queue_locker.lock();
    if (request_queue.size() > max_request_num){
        queue_locker.unlock();
        return false;  // 添加任务失败
    }

    request_queue.push_back(request);
    queue_locker.unlock();
    queue_stat.post();
    return true;
}

template<typename T>
void* Threadpool<T>::worker(void *arg){
    Threadpool* pool = (Threadpool*) arg ; // 强制类型转换
    //注意这里有点c++问题，pool应该是个对象，按说没办法直接访问类的私有函数，但这里可以？
    pool->run(); //启动线程工作逻辑
    return pool;
}

template<typename T>
void Threadpool<T>::run(){
    while(!stop){
        //执行业务逻辑，需要对请求队列中的任务请求进行操作，所以需要从线程池维护的请求队列中拿出
        //相应的请求任务，进行逻辑处理
        //先信号量
        queue_stat.wait();
        //再加锁
        queue_locker.lock();
        if (request_queue.empty()){
            queue_locker.unlock();
            continue;
        }
        T* request = request_queue.front(); // 拿出请求
        request_queue.pop_front(); //从请求队列中删除此请求
        queue_locker.unlock();
        if (request == NULL){  //可写可不写，都拿出请求了，应该肯定不为null
            continue;
        } 
        //执行此请求对应(要求)的操作，待后续实现
        request->process();
        // http_conn* conn = new http_conn;
        // conn->process();
    }
}

#endif