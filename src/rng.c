#include "rng.h"

#include <fcntl.h>
#include <unistd.h>
#include <time.h>

static uint64_t mix64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

void rng_init(struct Rng *rng) {
    if (!rng) {
        return;
    }
    uint64_t seed = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, &seed, sizeof(seed));
        close(fd);
        if (n != (ssize_t)sizeof(seed)) {
            seed = 0;
        }
    }
    if (seed == 0) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            ts.tv_sec = 0;
            ts.tv_nsec = 0;
        }
        seed = (uint64_t)ts.tv_nsec ^ ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)getpid();
    }
    rng->state = mix64(seed);
    if (rng->state == 0) {
        rng->state = 0x9e3779b97f4a7c15ULL;
    }
}

uint64_t rng_next_u64(struct Rng *rng) {
    if (!rng) {
        return 0;
    }
    uint64_t x = rng->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng->state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

uint32_t rng_next_u32(struct Rng *rng) {
    return (uint32_t)(rng_next_u64(rng) >> 32);
}

size_t rng_range(struct Rng *rng, size_t upper) {
    if (!rng || upper == 0) {
        return 0;
    }
    uint64_t threshold = (uint64_t)(-upper) % upper;
    for (size_t i = 0; i < RNG_RETRY_LIMIT; i++) {
        uint64_t r = rng_next_u64(rng);
        if (r >= threshold) {
            return (size_t)(r % upper);
        }
    }
    return (size_t)(rng_next_u64(rng) % upper);
}

void rng_shuffle_groups(struct Rng *rng, size_t *values, size_t count) {
    if (!rng || !values || count < 2) {
        return;
    }
    if (count > MAX_GROUPS) {
        count = MAX_GROUPS;
    }
    for (size_t i = 1; i < MAX_GROUPS; i++) {
        if (i >= count) {
            break;
        }
        size_t j = rng_range(rng, i + 1);
        size_t tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
    }
}

void rng_shuffle_items(struct Rng *rng, size_t *values, size_t count) {
    if (!rng || !values || count < 2) {
        return;
    }
    if (count > MAX_ITEMS_TOTAL) {
        count = MAX_ITEMS_TOTAL;
    }
    for (size_t i = 1; i < MAX_ITEMS_TOTAL; i++) {
        if (i >= count) {
            break;
        }
        size_t j = rng_range(rng, i + 1);
        size_t tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
    }
}
