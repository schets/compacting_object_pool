#ifndef COMPACTING_POOL_HPP
#define COMPACTING_POOL_HPP

#include <stddef.h>
#include <stdint.h>


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
        __asm("bsf %1, %0" : "=r" (val) : "r" : (val) :);
        return val;
    }

    static inline size_t get_and_clear_first_set(size_t *dest) {
        size_t oldval = *dest;
        size_t rval = get_first_set(oldval);
        // On haswell, one can use the clear-lowest-set instruction blsr
        // which only takes 1 cycle, would have no dependency on
        // get-first-set, and can execute on a different port.
        // (would require going from lowest bit to highest in other ops)
        __asm("btc %1, %0" : "=r" (oldval) : "r" (rval) :);
        *dest = oldval;
        return rval;
    }

    static inline size_t set_bit(size_t which, size_t val) {
        __asm("bts %1, %0" : "=r" (val) : "r" (which) :);
        return val;
    }

    struct dummmy_object {
        alignas(align) union {
            char data[size];
            dummy_object *next;
        }
    };

    struct slab {
        constexpr static size_t bits_per_size = sizeof(size_t) * 8;
        dummy_object members[bits_per_size];
        size_t open_bitmask;

        bool is_empty() const { return open_bitmask == 0; }

        size_t get_count() const { return __builtin_popcnt(open_bitmask); }

        dummy_object *get_object() {
            return &members[get_and_clear_first_set(open_bitmask)];
        }

        dummy_object *return_object(void *_obj) {
            dummy_object *obj = (dummy_object *)_obj;
            open_bitmask = set_bit(open_bitmask, obj - &members[0]);
        }

        static slab *lookup_slab(void *obj) {
            return (slab *)((size_t)obj & 4096);
        }
    };

    void *held_buffer[256];
    void *current;
    size_t retrieved_streak;
    uint8_t stack_head;

    void *try_get_obj() {
        void *rval = current;
        ++retrieved_streak;
        if (rval) {
            current = held_buffer[stack_head];
            held_buffer[stack_head--] = nullptr;
            return rval;
        }

        // add slab-management-code and bulk-load code here
        return nullptr;
    }

    void return_object(void *to_ret) {
        void *to_write = current;
        current = to_ret;
        retrieved_streak = 0;
        if (current) {
            void *old_val = held_buffer[++stack_head];
            held_buffer[stack_head] = current;
            if (old_val) {
                slab *s = slab::lookup_slab(old_val);
                s->return_object(old_val);
                // add some slab-management code here
            }
        }
    }
};


/// For global type_based pools, allows segregation
/// of a type from other pools with objects of the same size
class default_pool_tag {};

#endif
