#ifndef _SKETCH_H_
#define _SKETCH_H_

#ifdef __cplusplus
#   define __restrict__
extern "C" {
#endif

#include "types.h"

typedef struct sketch_item {
   ValueType value;
   bool latch;
} sketch_item;

typedef struct sketch {
   uint64_t      rows;
   uint64_t      cols;
   sketch_item  *table;
   unsigned int *hashes;
} sketch;

void
sketch_init(uint64_t rows, uint64_t cols, sketch *sktch);
void
sketch_deinit(sketch *sktch);

void
sketch_insert(sketch *sktch, KeyType key, ValueType value);
ValueType
sketch_get(sketch *sktch, KeyType key);

#ifdef __cplusplus
}
#endif

#endif