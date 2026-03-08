/*
  heap.hpp - kernel heap allocator interface
  kheap::alloc(bytes, align) and kheap::free(ptr)
  also provides placement new operators so you can do: new (kheap_tag) Foo()
*/
#pragma once
#include <stddef.h>

namespace kheap {

  void init();

  void* alloc(size_t bytes, size_t align = 16);

  void  free(void* ptr);

  size_t used_bytes();
  size_t free_bytes();
}

struct KHeapTag {};
inline constexpr KHeapTag kheap_tag{};
inline void* operator new  (size_t n, KHeapTag) { return kheap::alloc(n, 16); }
inline void* operator new[](size_t n, KHeapTag) { return kheap::alloc(n, 16); }
inline void  operator delete  (void* p, KHeapTag) noexcept { kheap::free(p); }
inline void  operator delete[](void* p, KHeapTag) noexcept { kheap::free(p); }
