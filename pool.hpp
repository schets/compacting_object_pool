#ifndef COMPACTING_POOL_HPP
#define COMPACTING_POOL_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "util.hpp"
/// This is the base class for allocating objects of a certain size
/// Parameters:
///     size: The size of each block being allocated
///     align: The minimum alignment of each object
///
/// The pools works at two levels:
///
/// 1. There's a stack in a ringbuffer which keeps track of the most
///    recently freed objects. This is done to keep the most recently
///    used objects as the most-recently allocated. When an object is allocated,
///    the most recent free is loaded (or loads from a slab) and the position made empty.
///    When an object's position in the pool is overwritten, that object is evicted back to
///    the block from which it came.
///
/// 2. Backing the fresh cache is a set of aligned slabs - each one contains
///    (16, 32, 64), however many bits are in a size_t objects. This is
///    done for a specific reason - it allows extremely easy lookup of the
///    first available object in sequential order, and also makes it possible
///    to store objects without allocating extra space per-object or modifying
///    the object after free. Allocating from a slab requires looking up the first
///    set, clearing it, and returning the corresponding object. Returning to a slab
///    looks up the slab from the object's address by masking lower-order bits and
///    sets the corresponding index in the bitmask.
///
/// I haven't fully planned out the slab pooling plan, but it will probably be
/// more like a simpler pool.
///
/// Why all this instead of a basic pool? It allows storing objects without modifying
/// them or adding extra fields, and it also means that the locality of objects in the
/// pool is less likely to get scrambled - only a small set of objects are held
/// unordered and when that cache is empty local elements are loaded from a slab. You
/// can return elements to main memory much more easily with this than with a standard
/// linked-stack type of pool.
/// A secondary advantage is that bulk-loading from a slab into the cache is that
/// each loop iteration ony depends on the value of the bitmask and not on
/// loads from a possibly uncached linked list of usable objects.
///
/// Hopefully I'll have this finished and tests to see if this is legit soon!
template<size_t size, size_t align>
class base_compacting_pool {

    static inline size_t get_first_set(size_t val) {
        __asm("bsf %1, %0" : "=r" (val) : "r" (val) :);
        return val;
    }

    static inline size_t get_and_clear_first_set(size_t *dest) {
        size_t oldval = *dest;
        size_t rval = get_first_set(oldval);
        // On haswell, one can use the clear-lowest-set instruction blsr
        // which only takes 1 cycle, would have no dependency on
        // get-first-set, and can execute on a different port.
        // (would require going from lowest bit to highest in other ops)
        __asm("btr %1, %0" : "=r" (oldval) : "r" (rval), "0" (oldval) :);
        *dest = oldval;
        return rval;
    }

    static inline size_t set_bit(size_t which, size_t val) {
        __asm("bts %1, %0" : "=r" (which) : "r" (val), "0" (which) :);
        return which;
    }

    struct dummy_object {
        alignas(align) char data[size];
    };

    constexpr static size_t bits_per_size = sizeof(size_t) * 8;
    constexpr static size_t all_ones = (0 - 1);

    struct slab {
        dummy_object members[bits_per_size];
        size_t open_bitmask;
        slab *next, *prev; //maintaining a linked list in the slab pool

        bool is_empty() const { return open_bitmask == 0; }

        size_t get_count() const { return __builtin_popcount(open_bitmask); }

        dummy_object *get_object() {
            return &members[get_and_clear_first_set(&open_bitmask)];
        }

        dummy_object *return_object(void *_obj) {
            dummy_object *obj = (dummy_object *)_obj;
            open_bitmask = set_bit(open_bitmask, obj - &members[0]);
        }

        static slab *lookup_slab(void *obj) {
            return (slab *)((size_t)obj & ~4095);
        }
    };

    constexpr static size_t partial_slabs = 0;
    constexpr static size_t full_slabs = 1;
    constexpr static size_t retrieval_limit = 10;

    void *current;
    uint32_t alloc_streak = 0;
    uint32_t evict_streak = 0;
    uint32_t load_streak = 0;
    uint8_t stack_head = 0;


    void *held_buffer[256];
    slab *empty_slabs;
    slab *data_slabs[2];

    void *add_slab() {
        slab *s;
        posix_memalign((void **)&s, 64, sizeof(*s));
        s->next = empty_slabs;
        if (empty_slabs) empty_slabs->prev = s;
        empty_slabs = s;
        s->prev = nullptr;
        s->open_bitmask = all_ones & ~1;
        load_all(s);
        return &s->members[0];
    }

    void *get_from_slab_list() {
        size_t which_slabs = partial_slabs;
        slab *tryit = data_slabs[which_slabs];
        tryit = (tryit == nullptr) ? data_slabs[which_slabs ^ 1] : tryit;
        evict_streak = 0;
        if (unlikely(tryit == nullptr)) {
            return nullptr;
        }
        void *rval = tryit->get_object();
        load_all(tryit);
        return rval;
    }

    template<bool do_malloc>
    void *base_try_alloc() {
        void *rval = current;
        ++alloc_streak;
        if (likely(rval)) {
            current = held_buffer[stack_head];
            held_buffer[stack_head--] = nullptr;
            return rval;
        }{
            void *rval = get_from_slab_list();
            return !rval && do_malloc ? add_slab() : rval;
        }
    }

    void load_all(slab *s) {
        uint64_t available_set = s->open_bitmask;
        while(true) {
            uint64_t index = get_and_clear_first_set(&available_set);
            void *value = &s->members[index];
            __builtin_prefetch(value, 1, 3);
            if (available_set) {
                held_buffer[++stack_head] = value;
            }
            else {
                current = value;
                break;
            }
        };
    }

    static void remove_slab(slab *s, slab *& head) {
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

public:
    void *try_alloc() {
        return base_try_alloc<false>();
    }

    void *alloc() {
        return base_try_alloc<true>();
    }

    void free(void *to_ret) {
        void *to_write = current;
        current = to_ret;
        if (likely(to_write)) {
            void *old_val = held_buffer[++stack_head];
            held_buffer[stack_head] = to_write;
            if (old_val) {
                // move common operations to a shared code space
                        ++evict_streak;
                        slab *s = slab::lookup_slab(old_val);
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
                                slab *partial = data_slabs[partial_slabs];
                                if (partial) {
                                    s->prev = partial->prev;
                                    s->next = partial;
                                    partial->prev = s;
                                    if (s->prev) {
                                        s->prev->next = s;
                                    }
                                }
                                else {
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
                                slab *full = data_slabs[full_slabs];
                                s->next = full;
                                if (full) {
                                    full->prev = s;
                                }
                                data_slabs[full_slabs] = s;
                            }
                        }
            }
        }
    }

    void clean() {
        slab *f = data_slabs[full_slabs];
        data_slabs[full_slabs] = nullptr;
        while(f) {
            slab *tofree = f;
            remove_slab(f, f);
            ::free(tofree);
        }
    }

    base_compacting_pool()
        :
        current(nullptr),
        stack_head(0),
        empty_slabs(nullptr)
        {
            data_slabs[0] = nullptr;
            data_slabs[1] = nullptr;
            for (int i = 0; i < 256; i++) {
                held_buffer[i] = nullptr;
            }
        }
};


/// For global type_based pools, allows segregation
/// of a type from other pools with objects of the same size
class default_pool_tag {};

#endif
