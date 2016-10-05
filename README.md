This is a pool allocator which commpacts long-freed memory without long pause times or serious overhead.

How does it work?

Like most pool allocators, this works by allocating large contiguous blocks of memory and divying out objecst from the,. Unlike a normal pool allocator which stores elements in a free list, this doesn't use any sort of intrusive datastructure for storing free elements. Instead, it has two levels:

 1. There's a stack inside of a ringbuffer to hold recently freed objects. This mimics the freelist structure but makes it easy to evict the least-recently-allocated item if the cacche fills up. When an item is inserted and the cache is full, the least-recently used object is then evicted back to it's block of origin. Conversely, when the cache is empty, a suitable block is selected (O1) and emptied into the cache.

 2. Each object is part of a large, 2^n aligned array. The array of origin can be found by simply masking off the bottom bits of a pointer. Inside this array, vacancy is determined by a bitmask. Retrieving and returning elements into the stack extremely simple and allows ordered retrieval:
  * An element is returned to a block simply by setting the corresponding bit to 1
  * An element is retrieved from a block by finding the first set bit, zeroing it, and returning the corresponding address.


Why all this instead of a basic pool? It allows storing objects without
modifying them or adding extra fields, and it also means that the locality
of objects in the pool is less likely to get scrambled - only a small set
of objects are held unordered and when that cache is empty local elements
are loaded from a slab. One can return elements to main memory much more
easily with this than with a standard freelist.
A secondary advantage is that bulk-loading from a slab into the cache is
each loop iteration ony depends on the value of the bitmask and not on
loads from a possibly uncached linked list of usable objects.

On one microbenchmark, this shows a 2x performance improvement for a large data size, although shows a small penalty when all the data fits in the L1 cache.
