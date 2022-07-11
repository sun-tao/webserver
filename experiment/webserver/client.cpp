#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>
using namespace std;

int main(){
    int fd = socket(AF_INET,SOCK_STREAM,0);
    if (fd == -1){
        perror("socket!");
        exit(-1);
    }
    struct sockaddr_in address;
    address.sin_family = AF_INET;  //协议
    inet_pton(AF_INET, "192.168.75.128", &address.sin_addr.s_addr);
    address.sin_port = htons(9999);
    int ret = connect(fd,(struct sockaddr*)& address,static_cast<socklen_t>(sizeof(address)));
    if (ret == -1){
        perror("connect");
        exit(-1);
    } 
    char buf[1024] = {0};
    while(true){
        const char * data = "hello";
        write(fd,data,strlen(data));
        sleep(1);

        //int len = read(fd,buf,5);
        // if (len > 0){
        //     cout << "recv data:" << endl;
        // }
        // else if (len == 0){
        //     cout << "服务器断开连接" << endl;
        //     break;
        // }
    }
    return 0;
}