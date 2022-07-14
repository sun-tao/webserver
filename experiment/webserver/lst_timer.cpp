#include "lst_timer.h"
#include <time.h>

void detect_outtimer(sort_timer_lst& timer_lst,
                    util_timer* nowtime){
    
    util_timer* tmp = timer_lst.head;
    while(tmp != nullptr){
        if (nowtime->expire < tmp->expire){
            break;
        }
        util_timer* head = tmp->next;
        timer_lst.del_timer(tmp);
        tmp = head;
    }
    return;
}
int main(){
    sort_timer_lst timer_lst;
    util_timer* timer1 = new util_timer(); 
    timer1->expire = (time_t) 1000;
    util_timer* timer2 = new util_timer();
    timer2->expire = (time_t) 2000;
    util_timer* timer3 = new util_timer();
    timer3->expire = (time_t) 1500;
    util_timer* timer4 = new util_timer();
    timer4->expire = (time_t) 500;
    util_timer* timer5 = new util_timer();
    timer5->expire = (time_t) 15000;
    timer_lst.add_timer(timer1);
    timer_lst.showElement();
    printf("=====================\n");
    
    timer_lst.del_timer(timer1);

    timer_lst.add_timer(timer2);

    //timer_lst.showElement();
    return 0;
}