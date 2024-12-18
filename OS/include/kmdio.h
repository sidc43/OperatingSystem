#pragma once

#define VGA_H 25
#define VGA_W 80
#define KB_DATA_PORT 0x60
#define ENTER 0x1c
#define BACKSPACE 0x0e
#define VGA_H 25
#define VGA_W 80
#define KB_DATA_PORT 0x60
#define ENTER 0x1c
#define BACKSPACE 0x0e
#define BLACK 0x00
#define BLUE 0x01
#define GREEN 0x02
#define CYAN 0x03
#define RED 0x04
#define MAGENTA 0x05
#define BROWN 0x06
#define LIGHT_GRAY 0x07
#define DARK_GRAY 0x08
#define LIGHT_BLUE 0x09
#define LIGHT_GREEN 0x0A
#define LIGHT_CYAN 0x0B
#define LIGHT_RED 0x0C
#define LIGHT_MAGENTA 0x0D
#define YELLOW 0x0E
#define WHITE 0x0F
#define VGA_COLOR(fg, bg) ((bg << 4) | (fg))

#ifndef size_t
    #if defined(__x86_64__) || defined(_M_X64)
        typedef unsigned long size_t;
    #else
        typedef unsigned int size_t;
    #endif
#endif

#include "kstdint.h"
#include "kstring.h"

/*
    Usage
        kout << "X = " << 4;
        kin >> buffer;
        clear() - clear console
*/

namespace kmdio 
{
    inline uint16_t cursor_x = 0, cursor_y = 0;
    inline uint16_t* VIDEO_MEMORY = (uint16_t*)0xB8000;

    inline uint8_t inb(uint16_t port) 
    {
        uint8_t ret;
        asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
        return ret;
    }

    inline void outb(uint16_t port, uint8_t value) 
    {
        asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
    }

    inline void itoa(int value, char* buffer) 
    {
        int i = 0;
        bool isNegative = (value < 0);

        if (isNegative) value = -value;

        do {
            buffer[i++] = '0' + (value % 10);
            value /= 10;
        } while (value);

        if (isNegative) buffer[i++] = '-';

        buffer[i] = '\0';

        for (int j = 0; j < i / 2; j++) {
            char temp = buffer[j];
            buffer[j] = buffer[i - j - 1];
            buffer[i - j - 1] = temp;
        }
    }

    inline void move_cursor() 
    {
        uint16_t pos = cursor_y * VGA_W + cursor_x;

        outb(0x3D4, 14);                 
        outb(0x3D5, (pos >> 8) & 0xFF);  
        outb(0x3D4, 15);                 
        outb(0x3D5, pos & 0xFF);         
    }

    inline void clear(int color = VGA_COLOR(WHITE, 0)) 
    {
        for (unsigned int i = 0; i < VGA_H * VGA_W; i++) 
        {
            VIDEO_MEMORY[i] = (uint16_t)' ' | (color << 8);
        }
        cursor_x = 0;
        cursor_y = 0;
        move_cursor();
    }

    inline void putc(char c) 
    {
        if (c == '\n') 
        { 
            cursor_x = 0; cursor_y++;
        } 
        else if (c == '\b') 
        {
            if (cursor_x > 0) 
            {
                cursor_x--;
                VIDEO_MEMORY[cursor_y * VGA_W + cursor_x] = (uint16_t)' ' | (WHITE << 8);
            }
        } 
        else 
        {
            VIDEO_MEMORY[cursor_y * VGA_W + cursor_x] = (uint16_t)c | (WHITE << 8);
            cursor_x++;
        }

        if (cursor_x >= VGA_W) { cursor_x = 0; cursor_y++; }
        if (cursor_y >= VGA_H) { clear(); } 
        move_cursor();
    }

    inline char getc() 
    {
        while (true) 
        {
            if (inb(0x64) & 0x01) 
            {
                uint8_t scancode = inb(KB_DATA_PORT);
                static const char scancode_ascii[] = {
                    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
                    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
                    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
                    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0
                };
                if (scancode < 128) return scancode_ascii[scancode];
            }
        }
    }

    class kout_stream 
    {
    public:
        kout_stream& operator<<(const char* str) 
        {
            while (*str) putc(*str++);
            return *this;
        }

        kout_stream& operator<<(int num) 
        {
            char buffer[12];
            itoa(num, buffer);
            return *this << buffer;
        }

        kout_stream& operator<<(const kstring& str) 
        {
            for (int i = 0; i < str.size; ++i) putc(str[i]);
            return *this;
        }
    };

    class kin_stream 
    {
    public:
        kin_stream& operator>>(char* buffer) 
        {
            int index = 0;
            while (true) 
            {
                char c = getc();
                if (c == '\n') break;
                if (c == '\b' && index > 0) 
                {
                    index--;
                    putc('\b');
                } 
                else if (c >= ' ') 
                { 
                    buffer[index++] = c;
                    putc(c);
                }
            }
            buffer[index] = '\0';
            return *this;
        }
    };

    inline kout_stream kout;
    inline kin_stream kin;
}