/* SPDX-License-Identifier: MIT */
#ifndef CRAM_LOG_H
#define CRAM_LOG_H

#include <stddef.h>

struct Session;

int log_open(const struct Session *session);
int log_close(const struct Session *session);

int log_input(const struct Session *session, const char *path);

int log_simple(const char *tag, const char *msg);
int log_key(int key);
int log_prompt(const struct Session *session, size_t group_index,
	       size_t item_index);
int log_group(const char *tag, size_t group_index);
int log_shuffle(const char *tag, size_t group_index);

#endif
