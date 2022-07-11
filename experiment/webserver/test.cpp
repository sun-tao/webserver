#include <iostream>
using namespace std;
class A{
public:
    static int a;
};

A::a = 0;

int main(){
    A::a ++ ;
    cout << A::a << endl;
}