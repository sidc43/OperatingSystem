#pragma once

/*
    Functionality
        Initialization
            kstring str("Message");

        Indexing
            str[i] - returns content[i]
        
        Utility
            len() - returns length of char*
            ctos() - convert char* to kstring
*/

class kstring 
{
    public:
        static constexpr int MAX_SIZE = 256;   
        char content[MAX_SIZE];               
        int size;                             

        kstring() : size(0) 
        {
            for (int i = 0; i < MAX_SIZE; i++) 
            {
                content[i] = '\0';
            }
        }

        kstring(int sz) : size(sz) 
        {
            if (size >= MAX_SIZE) size = MAX_SIZE - 1;
            for (int i = 0; i < MAX_SIZE; i++) 
            {
                content[i] = '\0';
            }
        }

        kstring(const char* str) : size(0) 
        {
            size = len(str);
            set(str);
        }

        char& operator[](int index) 
        {
            static char dummy = '\0';
            if (index < 0 || index >= size) return dummy;
            return content[index];
        }

        const char& operator[](int index) const 
        {
            static char dummy = '\0';
            if (index < 0 || index >= size) return dummy;
            return content[index];
        }

        void set(const char* str) 
        {
            size = 0; 
            while (str[size] != '\0' && size < MAX_SIZE - 1) 
            {
                content[size] = str[size];
                size++;
            }
            content[size] = '\0';
        }

        static int len(const char* str) 
        {
            int i = 0;
            while (str[i] != '\0') i++;
            return i;
        }

        static kstring ctos(const char* str) 
        {
            kstring s;           
            int n = len(str);    
            if (n >= kstring::MAX_SIZE) n = kstring::MAX_SIZE - 1; 

            for (int i = 0; i < n; i++) 
            {
                s[i] = str[i];   
            }
            s.size = n;          
            s.content[n] = '\0'; 
            return s;
        }
};
