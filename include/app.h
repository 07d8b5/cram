/* SPDX-License-Identifier: MIT */
#ifndef CRAM_APP_H
#define CRAM_APP_H

#include <stddef.h>

#include "config.h"
#include "model.h"
#include "rng.h"
#include "term.h"

struct app {
  struct Session session;
  struct TermState term;
  struct Rng rng;
  size_t group_order[MAX_GROUPS];
  size_t item_order[MAX_ITEMS_PER_GROUP];
};

int app_main(struct app* app, int argc, char** argv);
int app_run_file(struct app* app, const char* path);

#endif
