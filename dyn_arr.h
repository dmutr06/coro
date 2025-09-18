#include <stddef.h>
#include <string.h>

#ifndef DYN_ARR_H_
#define DYN_ARR_H_

#define DARR_INIT_CAP 64

#define DynArr(type...) \
  struct { \
    size_t size; \
    size_t cap; \
    type *items; \
    void (*deinit_item)(type *item); \
  }

#define darr_init(arr, deinit) darr_init_with_cap(arr, deinit, DARR_INIT_CAP) 

#define darr_init_with_cap(arr, deinit, cap_) do { \
  (arr)->cap = cap_; \
  (arr)->size = 0; \
  (arr)->items = malloc((cap_) * sizeof(*(arr)->items)); \
  (arr)->deinit_item = deinit; \
} while (0)

#define darr_push(arr, item) do { \
  if ((arr)->cap == 0) darr_init(arr, NULL); \
  if ((arr)->size >= (arr)->cap) { \
    darr_realloc(arr, (arr)->cap * 2); \
  } \
  (arr)->items[(arr)->size++] = item; \
} while (0)

#define darr_get(arr, idx) (((arr)->size > (idx) && (idx) >= 0) ? ((arr)->items + (idx)) : NULL)

#define darr_deinit(arr) do { \
  if ((arr)->deinit_item) \
    for (size_t i = 0; i < (arr)->size; ++i) { \
      (arr)->deinit_item((arr)->items + i); \
    } \
  free((arr)->items); \
} while (0) \

#define darr_pop(arr) do { \
  if ((arr)->size <= 0) break; \
  (arr)->size -= 1; \
  if ((arr)->deinit_item) \
    (arr)->deinit_item((arr)->items + (arr)->size); \
} while (0)

#define darr_remove(arr, idx) do { \
  if ((idx) < 0) break; \
  if ((idx) == (arr)->size - 1) { \
    darr_pop(arr); \
    break; \
  } \
  if ((idx) >= (arr)->size) break; \
  if ((arr)->deinit_item) \
    (arr)->deinit_item((arr)->items + (idx)); \
  memmove((arr)->items + (idx), (arr)->items + (idx) + 1, (--(arr)->size - (idx)) * sizeof(*(arr)->items)); \
} while (0)

#define darr_first(arr) darr_get(arr, 0)

#define darr_last(arr) darr_get(arr, (arr)->size - 1)

#define darr_foreach(type, arr, item) \
  for (type *item = (arr)->items; item < (arr)->items + (arr)->size; ++item)

#define darr_resize(arr, new_size) do { \
  if ((arr)->cap < new_size) { \
    (arr)->cap = new_size * 2; \
  } \
  if (new_size > (arr)->size) { \
    memset((arr)->items + ((arr)->size), 0, (new_size - (arr)->size) * sizeof(*(arr)->items)); \
  } \
  (arr)->size = new_size; \
} while (0)

#define darr_sort(arr, compare) \
  qsort((arr)->items, (arr)->size, sizeof(*(arr)->items), compare);

#define darr_push_slice(arr, slice, slice_size) do { \
  if (slice_size > (arr)->cap - (arr)->size) { \
    darr_realloc(arr, ((arr)->cap + (slice_size)) * 2); \
  } \
  memcpy((arr)->items + (arr)->size, slice, (slice_size) * sizeof(*(arr)->items)); \
  (arr)->size += slice_size; \
} while (0)

#define darr_realloc(arr, new_cap) do { \
  (arr)->cap = new_cap; \
  (arr)->items = realloc((arr)->items, (arr)->cap * sizeof(*(arr)->items)); \
  if ((arr)->cap < (arr)->size) (arr)->size = (arr)->cap; \
} while (0)

#endif // DYN_ARR_H_
