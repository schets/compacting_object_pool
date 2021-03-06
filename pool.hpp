#ifndef COMPACTING_POOL_HPP
#define COMPACTING_POOL_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "util.hpp"

#define assert(x)

/// This is the base class for allocating objects of a certain size
/// Parameters:
///     size: The size of each block being allocated
///     align: The minimum alignment of each object
///
/// The pools works at two levels:
///
/// 1. There's a stack in a ringbuffer which keeps track of the most
///    recently freed objects. This is done to keep the most recently
///    used objects as the most-recently allocated. When the stack is full,
///    a free causes the object at the bottom to be evicted.
///
/// 2. Backing the fresh cache is a set of aligned slabs - each one contains
///    (16, 32, 64), however many bits are in a size_t objects (more pending).
///    This detail allows extremely easy lookup of the first available object
///    in sequential order without pointer lookups and also makes it possible
///    to store objects without allocating extra space per-object or modifying
///    the object after free. Allocating from a slab requires looking up the
///
/// Why all this instead of a basic pool? It allows storing objects without
/// modifying them or adding extra fields, and it also means that the locality
/// of objects in the pool is less likely to get scrambled - only a small set
/// of objects are held unordered and when that cache is empty local elements
/// are loaded from a slab. One can return elements to main memory much more
/// easily with this than with a standard freelist.
/// A secondary advantage is that bulk-loading from a slab into the cache is
/// each loop iteration ony depends on the value of the bitmask and not on
/// loads from a possibly uncached linked list of usable objects.
template <size_t size, size_t align>
class base_compacting_pool {

  struct small_index {
    small_index(uint32_t v) : val(v) {}
    uint32_t val;
    constexpr static size_t mask = 63;
    void inc() {
      val = (val + 1) & mask;
    }

    void dec() {
      val = (val - 1) & mask;
    }

  };

  struct dummy_object {
    alignas(align) char data[size];
  };

  constexpr static size_t bits_per_size = sizeof(size_t) * 8;
  constexpr static size_t all_ones = (0 - 1);

  struct slab {
    dummy_object members[bits_per_size];
    size_t open_bitmask;
    slab* next, *prev;

    dummy_object* get_object() {
      dummy_object* obj = &members[get_and_clear_first_set(&open_bitmask)];
      return obj;
    }

    dummy_object* return_object(void* _obj) {
      dummy_object* obj = (dummy_object*)_obj;
      open_bitmask = set_bit(open_bitmask, obj - &members[0]);
    }

    static slab* lookup_slab(void* obj) { return (slab*)((size_t)obj & ~4095); }
  };

  constexpr static size_t partial_slabs = 0;
  constexpr static size_t full_slabs = 1;
  constexpr static size_t retrieval_limit = 10;

  void* current;
  uint32_t alloc_streak = 0;
  uint32_t evict_streak = 0;
  uint32_t load_streak = 0;
  small_index stack_head = 0;

  void* held_buffer[256];
  slab* empty_slabs;
  slab* data_slabs[2];

  void* add_slab() {
    slab* s;
    if (posix_memalign((void**)&s, 4096, sizeof(*s))) {
      return nullptr;
    }
    s->next = empty_slabs;
    if (empty_slabs)
      empty_slabs->prev = s;
    empty_slabs = s;
    s->prev = nullptr;
    s->open_bitmask = (0 - 1) ^ 1;
    // only called when empty!
    load_all(s);
    return s->members;
  }

  void* get_from_slab_list();



  void load_all(slab* s);

  static void remove_slab(slab* s, slab*& head) {
    if (s->prev == s) {
      head = nullptr;
      s->prev = s->next = nullptr;
      return;
    }
    if (s->prev) {
      s->prev->next = s->next;
    }
    if (s->next) {
      s->next->prev = s->prev;
    }

    if (s == head) {
      head = s->next;
    }
  }

  void evict_item(void *val);

  static void clean_slab_list(slab*& _s);

  template <bool do_malloc> void* base_try_alloc();

public:

  void* alloc() { return base_try_alloc<true>(); }

  void* try_alloc() { return base_try_alloc<false>(); }

  void clear_cache();


  void free(void* to_ret);


  void clean() { clean_slab_list(data_slabs[full_slabs]); }

  base_compacting_pool();
  ~base_compacting_pool();

};


template<size_t s, size_t a>
base_compacting_pool<s, a>::base_compacting_pool()
  : current(nullptr), stack_head(0), empty_slabs(nullptr) {
  data_slabs[0] = nullptr;
  data_slabs[1] = nullptr;
  for (auto& ptr : held_buffer) ptr = nullptr;
}

template<size_t s, size_t a>
base_compacting_pool<s, a>::~base_compacting_pool() {
  clear_cache();
  clean_slab_list(empty_slabs);
  clean_slab_list(data_slabs[partial_slabs]);
  clean_slab_list(data_slabs[full_slabs]);
}


template<size_t si, size_t a>
void base_compacting_pool<si, a>::clean_slab_list(slab*& _s) {
  slab* s = _s;
  _s = nullptr;
  while (s) {
    slab* tofree = s;
    remove_slab(s, s);
    ::free(tofree);
  }
}

template<size_t si, size_t a>
template<bool do_malloc>
void *base_compacting_pool<si, a>::base_try_alloc() {
  void* rval = current;
  ++alloc_streak;
  if (likely(rval)) {
    current = held_buffer[stack_head.val];
    held_buffer[stack_head.val] = nullptr;
    stack_head.dec();
    return rval;
  }
  {
    void* slabval = get_from_slab_list();
    return !slabval && do_malloc ? add_slab() : slabval;
  }
}

template<size_t si, size_t a>
void base_compacting_pool<si, a>::free(void *to_ret) {
  void* to_write = current;
  current = to_ret;
  if (likely(to_write)) {
    stack_head.inc();
    void* old_val = held_buffer[stack_head.val];
    held_buffer[stack_head.val] = to_write;
    if (old_val)
      evict_item(old_val);
  }
}

template<size_t si, size_t a>
void base_compacting_pool<si, a>::clear_cache() {
  small_index head = stack_head.val;
  stack_head.val = 0;
  if (current)
    evict_item(current);
  while (held_buffer[head.val]) {
    evict_item(held_buffer[head.val]);
    held_buffer[head.val] = nullptr;
    head.dec();
  }
}

template<size_t si, size_t a>
__attribute__ ((noinline)) void base_compacting_pool<si, a>::evict_item(void* old_val) {
  // move common operations to a shared code space
  ++evict_streak;
  slab* s = slab::lookup_slab(old_val);
  bool was_empty = s->open_bitmask == 0;
  s->return_object(old_val);
  bool val = s->open_bitmask == all_ones;
  val |= was_empty;

  // Only have one branch on the main path
  if (unlikely(val != 0)) {

    // empty slabs go to bottom of slab list
    // so that slabs evicted from the top are likely to be full
    // move slab from empty list to partial list
    if (was_empty) {
      remove_slab(s, empty_slabs);
      slab* partial = data_slabs[partial_slabs];
      if (partial) {
        s->prev = partial->prev;
        s->next = partial;
        partial->prev = s;
        if (s->prev) {
          s->prev->next = s;
        }
      } else {
        s->prev = s->next = nullptr;
        data_slabs[partial_slabs] = s;
      }
    }
    // full branches will go to top of list
    // since occupancy is all the same and there's
    // better cache properties
    else {
      remove_slab(s, data_slabs[partial_slabs]);
      s->prev = nullptr;
      slab* full = data_slabs[full_slabs];
      s->next = full;
      if (full) {
        full->prev = s;
      }
      data_slabs[full_slabs] = s;
    }
  }
}

template<size_t si, size_t a>
void *base_compacting_pool<si, a>::get_from_slab_list() {
  size_t which_slabs = partial_slabs;
  slab* tryit = data_slabs[which_slabs];
  tryit = (tryit == nullptr) ? data_slabs[which_slabs ^= 1] : tryit;
  // evict_streak = 0;
  if (unlikely(tryit == nullptr)) {
    return nullptr;
  }
  void* rval = tryit->get_object();
  if (tryit->open_bitmask) load_all(tryit);
  //evict to empty region!
  remove_slab(tryit, data_slabs[which_slabs]);
  if (empty_slabs) {
    empty_slabs->prev = tryit;
  }
  tryit->next = empty_slabs;
  empty_slabs = tryit;
  assert(tryit->open_bitmask == 0);
  return rval;
}

template<size_t si, size_t a>
void base_compacting_pool<si, a>::load_all(slab *s) {
  uint64_t available_set = s->open_bitmask;
  s->open_bitmask = 0;
  assert(available_set);
  while (true) {
    uint64_t index = get_and_clear_first_set(&available_set);
    void* value = &s->members[index];
    *(volatile uint32_t*)value;
    if (available_set) {
      stack_head.inc();
      held_buffer[stack_head.val] = value;
    } else {
      current = value;
      break;
    }
  };
}

/// For global type_based pools, allows segregation
/// of a type from other pools with objects of the same size
class default_pool_tag {};

#endif
