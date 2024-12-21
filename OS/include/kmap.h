#pragma once

#include "klist.h"

/*
    Functionality
        Initialization
            kmap<char, int> map;
        
        Utility
            map.insert(key, value) : add entry
            map.get(key, default_value) : default value is returned if the key is not found
            map.remove(key) : removes entry
            map.keys() : returns list of keys
            map.values() : returns list of values
*/

namespace collections
{
    template <typename K, typename V>
    class kmap
    {
        private:
            struct KeyValue
            {
                K key;
                V value;
            };
            klist<KeyValue> kv_pairs;
        
        public:
            // insert(key, value)
            void insert(K key, V value)
            {
                if (!contains(key))
                {
                    KeyValue kv;
                    kv.key = key;
                    kv.value = value;
                    kv_pairs.push(kv);
                }
            }

            // get(key)
            V get(K key, V default_val)
            {
                for (unsigned int i = 0; i < kv_pairs.length(); i++)
                {
                    if (kv_pairs[i].key == key)
                        return kv_pairs[i].value;
                }

                return default_val;
            }

            // remove(key)
            void remove(K key)
            {
                int index = find_index(key);
                if (index != -1)
                    kv_pairs.remove(index);

            }

            // contains(key)
            int find_index(K key)
            {
                for (unsigned int i = 0; i < kv_pairs.length(); i++)
                {
                    if (kv_pairs[i].key == key)
                        return i;
                }

                return -1;
            }

            bool contains(K key)
            {
                for (unsigned int i = 0; i < kv_pairs.length(); i++)
                {
                    if (kv_pairs[i].key == key)
                        return true;
                }

                return false;
            }

            // length()
            unsigned int length() const { return kv_pairs.length(); }

            // keys()
            klist<K> keys()
            {
                klist<K> k;
                for (unsigned int i = 0; i < kv_pairs.length(); i++)
                    k.push(kv_pairs[i].key);

                return k;
            }

            // values()
            klist<V> values()
            {
                klist<V> v;
                for (unsigned int i = 0; i < kv_pairs.length(); i++)
                    v.push(kv_pairs[i].value);

                return v;
            }

            V& operator[](unsigned int index) 
            {
                if (index >= kv_pairs.length()) 
                {
                    kernel_panic("Index out of bounds.");
                }
                return kv_pairs[index].value;
            }

            const V& operator[](unsigned int index) const 
            {
                if (index >= kv_pairs.length()) 
                {
                    kernel_panic("Index out of bounds.");
                }
                return kv_pairs[index].value;
            }

            friend kout_stream& operator<<(kout_stream& kstream, const kmap& m) 
            {
                kstream << "{" << endl;
                for (int i = 0; i < m.length(); i++)
                    kstream << "    " << m.kv_pairs[i].key << " : " << m.kv_pairs[i].value << "," << endl;

                kstream << "}";
                return kstream;
            }
    };
}