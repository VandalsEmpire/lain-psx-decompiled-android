#pragma once

#include <stddef.h>
#include <stdint.h>

extern "C" {
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
}

#include <cstdlib>
