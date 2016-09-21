#ifndef UTIL_HPP
#define UTIL_HPP

#define likely(x) __builtin_expect((!!(x)), 1)
#define unlikely(x) __builtin_expect((!!(x)), 0)

#define OUT_OF_LINE(block) [&, this]() __attribute__ ((noinline,hot)) block()
#define OUT_OF_LINE_COLD(block) [&, this]() __attribute__ ((noinline,cold)) block()

inline uint32_t inc_if_below_max(uint32_t inval) {
    __asm("cmp $21, %0\n\t"
          "adc $0, %0"
          :"=r" (inval)
          :"0" (inval) :);
    return inval;
}

inline uint32_t dec_if_above_min(uint32_t inval) {
    __asm("cmp $1, %0\n\t"
          "adc $-1, %0"
          :"=r" (inval)
          :"0" (inval) :);
    return inval;
}

#endif
