#ifndef CRAM_MODEL_H
#define CRAM_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

struct Item {
    uint32_t offset;
    uint32_t length;
};

struct Group {
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t seconds;
    uint32_t item_start;
    uint32_t item_count;
};

struct Session {
    char buffer[MAX_FILE_BYTES + 1];
    size_t buffer_len;
    struct Group groups[MAX_GROUPS];
    size_t group_count;
    struct Item items[MAX_ITEMS_TOTAL];
    size_t item_count;
};

int session_init(struct Session *session);

#endif
