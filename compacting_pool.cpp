#include "compacting_pool.h"

#include <stdlib.h>

#define noinline __attribute__ ((noinline))

namepsace compacting_pool {
namepsace compacting_pool_helpers {

    // Many of these functions are forcibly made not-inline because
    // inlining them would increase icache usage for a fairly small
    // gain in efficiency on the slow path - it might even increase
    // efficiency because a segment of code is much more likely
    // to be in the cache

    // These functions are also called in a way to minimize or even nullify the extra code

    slab_meta *create_slab(uint32_t size, uint32_t align) {

    }

    static noinline void remove_from_double_list(slab_meta *&list_head,
                                                 slab_meta *toret) {
        if (toret == list_head) {
            // in the case where there's only one element,
            // the following operations are a no-op. But I figure it's better
            // to keep an entry out of the branch predictor table
            // w/o a big increase in code size
            list_head = (toret == toret->next) ? nullptr : toret->next;
        }
        toret->next->prev = toret->prev;
        toret->prev->next = toret->next;
    }

    static noinline void add_to_double_list(slab_meta *&list_head,
                                            slab_meta *toret) {
        if (list_head) {
            // avoid extra loads/stores from aliasing
            slab_meta *cur_head = list_head;
            toret->next = cur_head;
            toret->prev = curret->prev;
            curret->prev->next = toret;
            cur_head->prev = toret;
        }
        else {
            toret->next = toret->prev = toret;
            list_head = toret;
        }
    }

    slab_meta *select_list_dump(slab_meta ** slabs, size_align align) {
        slab_meta *&empty_head = slabs[0];
        slab_meta *&partial_head = slabs[1];
        slab_meta *&full_head = slabs[2];

        // This set of branch *might* be compressible by transforming the boolean
        // into selection from a jump table. Not sure if that would confuse or help
        // the branch predictor or how much more valuable space in the
        // indirect branch predictor is...
        if (align.align > 0) {
            if (full_head) {
                goto get_full_slab;
            }
            else if (partial_head) {
                goto get_partial_slab;
            }
        }
        else {
            if (full_head) {
                goto get_full_slab;
            }
            else if (partial_head) {
                goto get_partial_slab;
            }
        }

        goto create_slab;
        // Gotos are bad!! except when they prevent gcc from blindly duplicating
        // almost identical code in a million different places to avoid a jump or a register mov

    get_partial_slab:

    get_full_slab:

    create_slab:
    }

} // namespace compacting_pool_helpers
} // namespace compacting_pool
