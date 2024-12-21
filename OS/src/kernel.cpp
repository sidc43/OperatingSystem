#include "../include/kmdio.h"
#include "../include/klist.h"
#include "../include/kmap.h"
#include "../include/ktuple.h"
#include "../include/kstdlib.h"
#include "../include/kfunctional.h"

using namespace kmdio;
using namespace collections;

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

    kmap<kstring, voidfunc0> no_param;
    kmap<kstring, voidfunc1> single_param;
    kmap<kstring, voidfunc2> double_param;

    kout << light_green << "Welcome to nullOS" << reset << endl;
    
    kstring input;

    while (true) 
    {
        if (input == "exit")
            kstdlib::exit_kernel(HALT);
        kout << "root> ";
        kin >> input;
        kout << "\nYou entered: " << light_cyan << input << reset << endl;
    }
}