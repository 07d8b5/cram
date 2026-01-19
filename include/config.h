/* SPDX-License-Identifier: MIT */
#ifndef CRAM_CONFIG_H
#define CRAM_CONFIG_H

#include <stddef.h>

#define MAX_GROUPS			65536U
#define MAX_ITEMS_TOTAL			1048576U
#define MAX_ITEMS_PER_GROUP		65536U
#define MAX_LINE_LEN			65536U
#define MAX_FILE_BYTES			(16U * 1024U * 1024U)
#define MAX_PROMPTS_PER_RUN		1048576U
#define MAX_WAIT_LOOPS			1048576U
#define MAX_GROUP_SECONDS		86400U
#define MAX_GROUP_MILLISECONDS \
	((unsigned long long)MAX_GROUP_SECONDS * 1000ULL)
#define RNG_RETRY_LIMIT			64U
#define MAX_WRITE_LOOPS			65536U

typedef unsigned int u32;
typedef unsigned long long u64;

/* compile-time constraints */
enum {
	static_assert_max_groups = 1 / ((MAX_GROUPS > 0) ? 1 : 0),
	static_assert_max_items_total = 1 / ((MAX_ITEMS_TOTAL > 0) ? 1 : 0),
	static_assert_max_items_per_group =
		1 / ((MAX_ITEMS_PER_GROUP > 0) ? 1 : 0),
	static_assert_items_per_group_le_total =
		1 / ((MAX_ITEMS_PER_GROUP <= MAX_ITEMS_TOTAL) ? 1 : 0),
	static_assert_max_line_len = 1 / ((MAX_LINE_LEN > 0) ? 1 : 0),
	static_assert_max_file_bytes = 1 / ((MAX_FILE_BYTES > 0) ? 1 : 0),
	static_assert_max_prompts_per_run =
		1 / ((MAX_PROMPTS_PER_RUN > 0) ? 1 : 0),
	static_assert_max_wait_loops = 1 / ((MAX_WAIT_LOOPS > 0) ? 1 : 0),
	static_assert_max_group_seconds =
		1 / ((MAX_GROUP_SECONDS > 0) ? 1 : 0),
	static_assert_max_group_ms =
		1 / (((MAX_GROUP_MILLISECONDS / 1000ULL) ==
		      (unsigned long long)MAX_GROUP_SECONDS) ?
			     1 :
			     0),
	static_assert_rng_retry_limit = 1 / ((RNG_RETRY_LIMIT > 0) ? 1 : 0),
	static_assert_max_write_loops = 1 / ((MAX_WRITE_LOOPS > 0) ? 1 : 0),
};

static inline int assert_ok(int cond)
{
	return cond ? 1 : 0;
}

static inline int assert_ptr(const void *ptr)
{
	return ptr ? 1 : 0;
}

static inline int validate_ok(int cond)
{
	return cond ? 1 : 0;
}

static inline int validate_ptr(const void *ptr)
{
	return ptr ? 1 : 0;
}

#endif
