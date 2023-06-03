#if !defined(_GNU_SOURCE)
#   define _GNU_SOURCE
#endif

#include "sketch.h"
#include "hashutil.h"
#include <sys/mman.h>
#include <assert.h>
#include <stdio.h>
#include <immintrin.h>

void
sketch_init(uint64_t rows, uint64_t cols, sketch *sktch)
{
   assert(sktch);

   sktch->rows = rows;
   sktch->cols = cols;

   sktch->table =
      (sketch_item *)mmap(NULL,
                        sktch->rows * sktch->cols * sizeof(sketch_item),
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
                        0,
                        0);
   if (!sktch->table) {
      perror("table malloc failed");
      exit(1);
   }

   sktch->hashes =
      (unsigned int *)mmap(NULL,
                           sktch->rows * sizeof(unsigned int),
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
                           0,
                           0);
   if (!sktch->hashes) {
      perror("hashes malloc failed");
      exit(1);
   }

   for (uint64_t row = 0; row < sktch->rows; ++row) {
      sktch->hashes[row] = 0xc0ffee + row;
   }
}

void
sketch_deinit(sketch *sktch)
{
   munmap(sktch->table, sktch->rows * sktch->cols * sizeof(sketch_item));
   munmap(sktch->hashes, sktch->rows * sizeof(unsigned int));
}

inline uint64_t
get_index_in_row(sketch *sktch, KeyType key, uint64_t row)
{
   uint64_t col =
      MurmurHash64A_inline((const void *)key, KEY_SIZE, sktch->hashes[row])
      % sktch->cols;
   return row * sktch->cols + col;
}

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))


static inline void
lock(bool *lck)
{
   while (__atomic_test_and_set(lck, __ATOMIC_ACQUIRE)) {
      _mm_pause();
   }
}

static inline void
unlock(bool *lck)
{
   __atomic_clear(lck, __ATOMIC_RELEASE);
}

void
sketch_insert(sketch *sktch, KeyType key, ValueType value)
{
   uint64_t index;
   for (uint64_t row = 0; row < sktch->rows; ++row) {
      index               = get_index_in_row(sktch, key, row);
      lock(&sktch->table[index].latch);
      sktch->table[index].value = MAX(sktch->table[index].value, value);
      unlock(&sktch->table[index].latch);
   }
}

ValueType
sketch_get(sketch *sktch, KeyType key)
{
   uint64_t  row   = 0;
   uint64_t  index = get_index_in_row(sktch, key, row);
   lock(&sktch->table[index].latch);
   ValueType value = sktch->table[index].value;
   unlock(&sktch->table[index].latch);
   for (row = 1; row < sktch->rows; ++row) {
      index = get_index_in_row(sktch, key, row);
      lock(&sktch->table[index].latch);
      value = MIN(sktch->table[index].value, value);
      unlock(&sktch->table[index].latch);
   }
   return value;
}