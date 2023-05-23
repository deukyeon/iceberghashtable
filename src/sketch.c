#if !defined(_GNU_SOURCE)
#   define _GNU_SOURCE
#endif

#include "sketch.h"
#include "hashutil.h"
#include <sys/mman.h>
#include <assert.h>
#include <stdio.h>

void
sketch_init(uint64_t rows, uint64_t cols, sketch *sktch)
{
   assert(sktch);

   sktch->rows = rows;
   sktch->cols = cols;

   sktch->table =
      (ValueType *)mmap(NULL,
                        sktch->rows * sktch->cols * sizeof(ValueType),
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
   munmap(sktch->table, sktch->rows * sktch->cols * sizeof(ValueType));
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

void
sketch_insert(sketch *sktch, KeyType key, ValueType value)
{
   uint64_t index;
   for (uint64_t row = 0; row < sktch->rows; ++row) {
      index               = get_index_in_row(sktch, key, row);
      sktch->table[index] = MAX(sktch->table[index], value);
   }
}

ValueType
sketch_get(sketch *sktch, KeyType key)
{
   uint64_t  row   = 0;
   uint64_t  index = get_index_in_row(sktch, key, row);
   ValueType value = sktch->table[index];
   for (row = 1; row < sktch->rows; ++row) {
      index = get_index_in_row(sktch, key, row);
      value = MIN(sktch->table[index], value);
   }
   return value;
}