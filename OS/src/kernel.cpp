#include "../include/kmdio.h"
#include "../include/klist.h"

using namespace kmdio;

typedef void (*constructor)();

extern "C" constructor __start_ctors; 
extern "C" constructor __end_ctors;   

extern "C" void call_global_constructors() 
{
    for (constructor* ctor = &__start_ctors; ctor != &__end_ctors; ++ctor) 
    {
        (*ctor)();
    }
}

extern "C" void kmain(void) 
{
    clear(WHITE); 
    malloc_init();

    char buffer[100];

    list<int> l;
    l.push(4454);
    l.push(3);
    l.push(2);
    l.push(1);
    l.push(5);

    kout << "welcome to nullOS!";

    while (true) 
    {
        kout << "\nroot> ";
        kin >> buffer;
        kout << "\nYou entered: " << buffer;
    }
}