#pragma once

#include "kstring.h"
/*
    Function types:
        [type]func[num_args]
        intfunc2
        voidfunc1
*/

typedef void (*voidfunc0)();
typedef void (*voidfunc1)(kstring);
typedef void (*voidfunc2)(kstring, kstring);