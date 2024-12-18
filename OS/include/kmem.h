#define NULL 0
#define ALIGN4(x) (((((x)-1)>>2)<<2)+4)
#define HEAP_SIZE 0x100000 

#include "kmdio.h"

using namespace kmdio;

typedef struct Block 
{
    size_t size;          
    int free;             
    struct Block* next;   
} Block;

void* kernel_heap_start = (void*)0x100000;
Block* free_list_head = NULL;

void kernel_panic(const char* message) 
{
    //kout << ("KERNEL PANIC: ");
    //kout << (message);
    //kout << ("\nSystem Halted.");

    while (true) 
    {
        __asm__("hlt");
    }
}

void malloc_init() 
{
    free_list_head = (Block*)kernel_heap_start; 
    free_list_head->size = HEAP_SIZE - sizeof(Block);
    free_list_head->free = 1;
    free_list_head->next = NULL;
}

Block* find_free_block(Block** last, size_t size) 
{
    Block* current = free_list_head;
    while (current && !(current->free && current->size >= size)) 
    {
        *last = current;
        current = current->next;
    }
    return current;
}

void split_block(Block* block, size_t size) 
{
    Block* new_block = (Block*)((char*)block + sizeof(Block) + size);
    new_block->size = block->size - size - sizeof(Block);
    new_block->free = 1;
    new_block->next = block->next;

    block->size = size;
    block->next = new_block;
}

void* malloc(size_t size) 
{
    if (size <= 0) return NULL;

    size = ALIGN4(size); 
    Block* block;

    if (!free_list_head) 
    {
        malloc_init(); 
    }

    Block* last = free_list_head;
    block = find_free_block(&last, size);

    if (block) 
    {
        if (block->size > size + sizeof(Block)) 
        {
            split_block(block, size);
        }
        block->free = 0;
        return (char*)block + sizeof(Block);
    }

    return NULL; 
}

void free(void* ptr) 
{
    if (!ptr) return;

    Block* block = (Block*)((char*)ptr - sizeof(Block));
    block->free = 1;

    Block* current = free_list_head;
    while (current) 
    {
        if (current->free && current->next && current->next->free) 
        {
            current->size += current->next->size + sizeof(Block);
            current->next = current->next->next;
        }
        current = current->next;
    }
}


void* operator new(size_t size) 
{
    void* ptr = malloc(size); 
    if (!ptr) 
    {
        kernel_panic("Out of memory during allocation.");
        while (1) {}
    }
    return ptr;
}

void operator delete(void* ptr) noexcept 
{
    if (ptr) 
    {
        free(ptr); 
    }
}

void* operator new[](size_t size) 
{
    void* ptr = malloc(size);
    if (!ptr) 
    {
        kernel_panic("Out of memory during allocation.");
        while (1) {}
    }
    return ptr;
}

void operator delete[](void* ptr) noexcept 
{
    if (ptr) 
    {
        free(ptr); 
    }
}