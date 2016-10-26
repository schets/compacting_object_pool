#ifndef COMPACTING_POOL_H
#define COMPACTING_POOL_H

#include <stdint.h>


// A compacting pool for 64 bit intel machines
// Support for different platforms in-progress
namespace compacting_pool {
namespace compacting_pool_helpers {

    struct object_meta {
        object_meta *next, *prev;
    }

    struct slab_meta {
        uint64_t open_mask;
        slab_meta *prev, *next;
    }

    // This is a more compact representation
    // of the alignment info when passing as a parameter
    struct size_align {
        uint32_t size;
        int32_t align;
    };

} // namespace compacting_pool_helpers

// Would be best to have evict_default not result
// in different sets of slabs, but oh well. probably not a big deal

template<unsigned char sz>
class compacting_pool {
    struct dummy_object {
        union {
            object_meta ometa; // also provides alignment properties
            char data[sz];
        }
    }

    struct slab {
        dummy_object members[64];
        slab_meta smeta;
    };

    // important that this is the first element
    // so the compare in alloc is directly compared to this
    object_meta sentinel;
    object_meta *head;
    int occupancy;

    void free_evict(object_meta *toret) {
        toret->prev = head;
        head->next = toret;
        head = toret;
        if (occupancy < 32) {
            ++occupancy;
        }
        else {
            // sentinel->next is the least recently inserted
            object_meta *toret =
        }
    }

    // only insert the head if there's
    void free_noevict(object_meta *toret) {
        if (occupancy < 32) {
            ++occupancy;
            toret->prev = head;
            head->next = toret;
            head = toret;
        }
        else {
            // load head to make sure it's in the cache
            (volatile object_meta *)head->next;
            // return toret to the cache
            //...
        }
    }

public:

    void *alloc() {
        object_meta *rval = head;
        if (rval != &sentinel) {
            ++occupancy;
            head = rval->prev;
            return rval;
        }
        else {
            // get new object
        }
    }

    template<bool evict = evict_default>
    void free(void *toret) {
        object_meta *meta = (object_meta *)toret;
        evict ? free_evict(meta) : free_noevict(meta);
    }
};

} // namespace compacting_pool
#endif
