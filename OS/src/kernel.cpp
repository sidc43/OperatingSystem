#include "../include/kmdio.h"
#include "../include/klist.h"
#include "../include/kmap.h"
#include "../include/ktuple.h"
#include "../include/kstdlib.h"

using namespace kmdio;
using namespace collections;

typedef void (*constructor)();
typedef void (*void_function)();

extern "C" constructor __start_ctors; 
extern "C" constructor __end_ctors;   

extern "C" void call_global_constructors() 
{
    for (constructor* ctor = &__start_ctors; ctor != &__end_ctors; ++ctor) 
    {
        (*ctor)();
    }
}

void hello()
{
    kout << "Hello" << endl;
}

extern "C" void kmain(void) 
{
    clear(WHITE); 
    malloc_init();
    
    char buffer[100];

    kout << "welcoe to nullOS" << endl;
    
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