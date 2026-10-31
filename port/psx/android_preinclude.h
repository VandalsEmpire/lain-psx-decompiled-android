#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

extern void *malloc(size_t size);
extern void *calloc(size_t count, size_t size);
extern void free(void *ptr);
