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

        void pop()
        {
            T removed = data[size];
            if (capacity > 0)
            {
                T* new_data = new T[capacity];

                for (unsigned int i = 0; i < size; i++) 
                {
                    new_data[i] = data[i];
                }

                delete[] data;
                data = new_data;
                size--;
                //return removed;
            }
            else
            {
                kernel_panic("List is too small to pop.");
            }
        }

        void remove(unsigned int index)
        {
            T removed = data[index];
            if (capacity > 0)
            {
                T* new_data = new T[capacity];

                for (unsigned int i = 0; i < index; i++)
                {
                    new_data[i] = data[i];
                }

                for (unsigned int i = index + 1; i < size; i++)
                {
                    new_data[i - 1] = data[i];
                }

                delete[] data;
                size--;
                data = new_data;
               // return removed;
            }
            else
            {
                kernel_panic("List is too small to pop.");
            }
        }

        void insert(unsigned int index, const T& value) 
        {
            if (index > size) 
            {
                kernel_panic("Index out of bounds."); 
            }

            if (size == capacity) 
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

            for (unsigned int i = size; i > index; i--) 
            {
                data[i] = data[i - 1];
            }

            data[index] = value;

            size++; 
        }

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

        //friend kout_stream& operator<<(kout_stream& kstream, const list& l) 
        //{
        //    kstream << "[";
        //    for (int i = 0; i < l.size - 1; i++)
        //        kstream << l[i] << ", ";
//
        //    kstream << l[l.size - 1] << "]";
        //    return kstream;
        //}
};