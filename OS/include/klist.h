#pragma once

#include "kmem.h"

template <typename T>
/*
    IN PROGRESS
    TODO:
        remove
        insert
        pop
        print
*/
class list
{
    private:
        T* data;
        unsigned int size;
        unsigned int capacity;

        void grow() 
        {
            capacity = (capacity == 0) ? 1 : capacity * 2;
            T* new_data = new T[capacity];

            for (unsigned int i = 0; i < size; i++) 
            {
                new_data[i] = data[i];
            }

            delete[] data;
            data = new_data;
        }

    public:
        list()
        {
            capacity = 1;
            size = 0;
            data = new T[capacity];
        }

        void push(T value) 
        {
            if (size >= capacity) 
            {
                grow();
            }
            data[size] = value;
            size++;
        }

        T pop();
        T remove(unsigned int index);
        void insert(unsigned int index);

        T& operator[](unsigned int index) 
        {
            if (index >= size) 
            {
                kernel_panic("Index out of bounds.");
            }
            return data[index];
        }

        const T& operator[](unsigned int index) const 
        {
            if (index >= size) 
            {
                kernel_panic("Index out of bounds.");
            }
            return data[index];
        }
};