#include "../include/kmdio.h"
#include "../include/klist.h"
#include "../include/kmap.h"
#include "../include/ktuple.h"
#include "../include/kstdlib.h"

using namespace kmdio;
using namespace collections;

/*
    Function types:
        [type]func[num_args]
        intfunc2
        voidfunc1
*/

typedef void (*constructor)();
typedef void (*voidfunc1)(kstring);

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

    kout << light_green << "Welcome to nullOS" << reset << endl;
    
    kstring input;

    while (true) 
    {
        if (input == "exit")
            kstdlib::exit_kernel(0);
        kout << "root> ";
        kin >> input;
        kout << "\nYou entered: " << input << endl;
    }
}