#include "../include/kmdio.h"

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

    char buffer[100];

    kstring msg("Welcome to nullOS!\n");
    kout(msg);


    while (true) 
    {
        kout("\nroot> ");
        
        kin(buffer, sizeof(buffer));
        kout("\nYou entered: ");
        kout(buffer);
    }
}