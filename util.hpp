#ifndef UTIL_HPP
#define UTIL_HPP

#define likely(x) __builtin_expect((!!(x)), 1)
#define unlikely(x) __builtin_expect((!!(x)), 0)

#define OUT_OF_LINE(block) [&, this]() __attribute__ ((noinline,hot)) block()
#define OUT_OF_LINE_COLD(block) [&, this]() __attribute__ ((noinline,cold)) block()

// Cheap saturating arithmetic (but for compile time values)

// should benchmark against cmp/setcc/add combo
// same theoretical cycles, but larger code than this? Decode bandwidth?
// I believe it depends on the cpu. Each one requires asm
// since the compilers generate fairly bad code for for this

// I should try byte operations, although I've read they are worse for
// decode bandwidth. Wil need real benchmarks up!

// I also need to modify the mins/maxs and adc/sbb values
// so that they regress towards the middle, and will only
// reach the maximum value once there are seriously many more
// evictions/allocs in a row
inline uint32_t inc_if_below_max(uint32_t inval) {
    __asm("cmp $21, %0\n\t"
          "adc $0, %0"
          :"=r" (inval)
          :"0" (inval) :);
    return inval;
}

inline uint32_t dec_if_above_min(uint32_t inval) {
    __asm("cmp $1, %0\n\t"
          "sbb $0, %0"
          :"=r" (inval)
          :"0" (inval) :);
    return inval;
}

#endif
