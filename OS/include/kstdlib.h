#pragma once

#define HALT 0
#define REBOOT 1
#define POWER_OFF 2

#include "kmdio.h"

using namespace kmdio;

namespace kstdlib
{
    inline void halt_cpu() 
    {
        asm volatile("hlt");
    }

    inline void reboot() 
    {
        asm volatile("cli");
        asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"(0x64));

    }

    inline void power_off() 
    {
        asm volatile("cli");
        asm volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"(0x604));
    }

    inline void exit_kernel(int mode) 
    {
        switch (mode) 
        {
            case HALT: 
                kout << light_red << "Halting the CPU...\n" << reset;
                kstdlib::halt_cpu();
                break;

            case REBOOT: 
                kout << "Rebooting the system...\n";
                kstdlib::reboot();
                break;

            case POWER_OFF: 
                kout << "Powering off the system...\n";
                kstdlib::power_off();
                break;

            default:
                kout << "Invalid mode. Halting by default...\n";
                kstdlib::halt_cpu();
                break;
        }
    }
}