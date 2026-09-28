#pragma once
#include <cstddef>
inline bool countButtonAllocations = false;
inline size_t buttonAllocations = 0;
inline size_t buttonAllocationBytes = 0;
void* allocateButtonProbe(size_t size);
void freeButtonProbe(void* pointer);
