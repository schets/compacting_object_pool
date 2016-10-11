#ifndef UTIL_HPP
#define UTIL_HPP

#define likely(x) __builtin_expect((!!(x)), 1)
#define unlikely(x) __builtin_expect((!!(x)), 0)

#define OUT_OF_LINE(block) [&, this]() __attribute__ ((noinline,hot)) block()
#define OUT_OF_LINE_COLD(block) [&, this]() __attribute__ ((noinline,cold)) block()

#define assert(x)

static inline size_t get_first_set(size_t val) {
    __asm("bsf %1, %0" : "=r"(val) : "r"(val) :);
    return val;
}

static inline size_t get_and_clear_first_set(size_t* dest) {
    size_t oldval = *dest;
    assert(oldval > 0);
    size_t rval = get_first_set(oldval);
    assert(rval < 64);
    // On haswell, one can use the clear-lowest-set instruction blsr
    // which only takes 1 cycle, would have no dependency on
    // get-first-set, and can execute on a different port.
    __asm("btr %1, %0" : "=r"(oldval) : "r"(rval), "0"(oldval) :);
    assert((oldval & ((size_t)1 << rval)) == 0);
    *dest = oldval;
    return rval;
}

static inline size_t set_bit(size_t which, size_t val) {
    assert(val < 64);
    assert((which & ((size_t)1 << val)) == 0);
    __asm("bts %1, %0" : "=r"(which) : "r"(val), "0"(which) :);
    assert((which & ((size_t)1 << val)) != 0);
    return which;
  }

static inline void set_bit_mem(size_t *which, size_t val) {
    assert(val < 64);
    assert((*which & ((size_t)1 << val)) == 0);
    __asm("bts %1, %0" : "=m"(*which) : "r"(val) :);
    assert((*which & ((size_t)1 << val)) != 0);
}
#endif
