#ifndef CRAM_RNG_H
#define CRAM_RNG_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

struct Rng {
    uint64_t state;
};

void rng_init(struct Rng *rng);
uint64_t rng_next_u64(struct Rng *rng);
uint32_t rng_next_u32(struct Rng *rng);
size_t rng_range(struct Rng *rng, size_t upper);
void rng_shuffle_groups(struct Rng *rng, size_t *values, size_t count);
void rng_shuffle_items(struct Rng *rng, size_t *values, size_t count);

#endif
