/* SPDX-License-Identifier: MIT */
#ifndef CRAM_RUNNER_H
#define CRAM_RUNNER_H

#include <stddef.h>

struct Session;
struct Rng;
struct TermState;

int runner_run(struct TermState* term,
    struct Session* session,
    struct Rng* rng,
    size_t* group_order,
    size_t* item_order);

#endif
