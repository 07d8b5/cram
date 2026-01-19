// SPDX-License-Identifier: MIT
#include "runner.h"
#include "config.h"
#include "log.h"
#include "model.h"
#include "rng.h"
#include "term.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct runtime {
  size_t order_pos;
  size_t group_index;
  size_t item_pos;
  size_t item_index;
  u64 group_end;
  int pending_switch;
};

struct ctx {
  struct Session* session;
  struct Rng* rng;
  size_t* group_order;
  size_t* item_order;
};

static int assert_session_bounds(const struct Session* session) {
  if (!validate_ptr(session))
    return -1;
  if (!assert_ok(session->group_count > 0))
    return -1;
  if (!assert_ok(session->group_count <= MAX_GROUPS))
    return -1;

  for (size_t i = 0; i < MAX_GROUPS; i++) {
    if (i >= session->group_count)
      break;
    size_t count = session->groups[i].item_count;
    if (!assert_ok(count > 0))
      return -1;
    if (!assert_ok(count <= MAX_ITEMS_PER_GROUP))
      return -1;
  }
  return 0;
}

static int now_ms(u64* out_ms) {
  if (!validate_ptr(out_ms))
    return -1;

  struct timespec ts;
  int rc = clock_gettime(CLOCK_MONOTONIC, &ts);

  if (rc != 0)
    return -1;
  *out_ms = (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000ULL);
  return 0;
}

static int draw_prompt(const struct Session* session, size_t item_index) {
  if (!validate_ptr(session))
    return -1;
  if (!assert_ok(item_index < session->item_count))
    return -1;
  if (!assert_ok(session->buffer_len > 0))
    return -1;

  struct Item item = session->items[item_index];

  if (!assert_ok(item.length > 0))
    return -1;
  if (!assert_ok(
          (size_t)item.offset + (size_t)item.length <= session->buffer_len))
    return -1;

  int rc = term_clear_screen();

  if (rc != 0)
    return -1;

  const char* text = session->buffer + item.offset;
  size_t written = fwrite(text, 1, item.length, stdout);

  if (!assert_ok(written == item.length))
    return -1;
  rc = fputc('\n', stdout);
  if (!assert_ok(rc != EOF))
    return -1;
  rc = fflush(stdout);
  if (rc != 0)
    return -1;
  return 0;
}

static int is_advance_key(int key) {
  if (!validate_ok(key >= 0))
    return 0;
  if (!validate_ok(key <= 255))
    return 0;
  if (key == ' ' || key == '\r' || key == '\n')
    return 1;
  return isalnum((unsigned char)key) != 0;
}

static int init_group_order(const struct ctx* c) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(c->session))
    return -1;
  if (!validate_ptr(c->group_order))
    return -1;
  if (assert_session_bounds(c->session) != 0)
    return -1;

  for (size_t i = 0; i < MAX_GROUPS; i++) {
    if (i >= c->session->group_count)
      break;
    c->group_order[i] = i;
  }
  return 0;
}

static int init_item_order(const struct ctx* c, size_t group_index) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(c->session))
    return -1;
  if (!validate_ptr(c->item_order))
    return -1;
  if (!assert_ok(group_index < c->session->group_count))
    return -1;

  struct Session* session = c->session;
  const struct Group* group = &session->groups[group_index];
  size_t count = group->item_count;
  size_t start = group->item_start;

  if (!assert_ok(count > 0))
    return -1;
  if (!assert_ok(count <= MAX_ITEMS_PER_GROUP))
    return -1;

  for (size_t i = 0; i < MAX_ITEMS_PER_GROUP; i++) {
    if (i >= count)
      break;
    c->item_order[i] = start + i;
  }
  return 0;
}

static int select_next_group(const struct ctx* c, struct runtime* rt) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(c->session))
    return -1;
  if (!validate_ptr(c->rng))
    return -1;
  if (!validate_ptr(c->group_order))
    return -1;
  if (!assert_ok(c->session->group_count > 0))
    return -1;

  if (rt->order_pos >= c->session->group_count) {
    int rc =
        rng_shuffle_groups(c->rng, c->group_order, c->session->group_count);
    if (rc != 0)
      return -1;
    rt->order_pos = 0;
    rc = log_simple("shuffle", "groups");
    if (rc != 0)
      return -1;
  }
  rt->group_index = c->group_order[rt->order_pos];
  if (!assert_ok(rt->group_index < c->session->group_count))
    return -1;
  rt->order_pos++;
  return 0;
}

static int select_next_item(const struct ctx* c, struct runtime* rt) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(c->session))
    return -1;
  if (!validate_ptr(c->item_order))
    return -1;
  if (!assert_ok(rt->group_index < c->session->group_count))
    return -1;

  struct Session* session = c->session;
  size_t group_index = rt->group_index;
  const struct Group* group = &session->groups[group_index];
  size_t count = group->item_count;

  if (!assert_ok(count > 0))
    return -1;
  if (!assert_ok(count <= MAX_ITEMS_PER_GROUP))
    return -1;

  if (rt->item_pos >= count)
    rt->item_pos = 0;
  rt->item_index = c->item_order[rt->item_pos];
  return 0;
}

static int update_group_timer(const struct ctx* c, struct runtime* rt) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(c->session))
    return -1;
  if (!assert_ok(rt->group_index < c->session->group_count))
    return -1;

  struct Session* session = c->session;
  size_t group_index = rt->group_index;
  const struct Group* group = &session->groups[group_index];
  unsigned int seconds = group->seconds;

  if (!assert_ok(seconds > 0))
    return -1;
  if (!assert_ok(seconds <= MAX_GROUP_SECONDS))
    return -1;

  u64 now = 0;
  int rc = now_ms(&now);

  if (rc != 0)
    return -1;
  rt->group_end = now + (u64)seconds * 1000ULL;
  return 0;
}

static int advance_prompt(
    const struct ctx* c, struct runtime* rt, int due_to_switch) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(c->session))
    return -1;
  if (!validate_ptr(c->rng))
    return -1;
  if (!assert_ok(rt->group_index < c->session->group_count))
    return -1;

  struct Session* session = c->session;
  size_t group_index = rt->group_index;
  const struct Group* group = &session->groups[group_index];
  size_t count = group->item_count;

  if (!assert_ok(count > 0))
    return -1;
  if (!assert_ok(count <= MAX_ITEMS_PER_GROUP))
    return -1;

  if (due_to_switch) {
    int rc = init_item_order(c, rt->group_index);

    if (rc != 0)
      return -1;
    rc = rng_shuffle_items(c->rng, c->item_order, count);
    if (rc != 0)
      return -1;
    rt->item_pos = 0;
    rc = update_group_timer(c, rt);
    if (rc != 0)
      return -1;
    rc = log_group("group", rt->group_index);
    if (rc != 0)
      return -1;
  } else {
    rt->item_pos++;
    if (rt->item_pos >= count) {
      int rc = rng_shuffle_items(c->rng, c->item_order, count);
      if (rc != 0)
        return -1;
      rt->item_pos = 0;
      rc = log_shuffle("items", rt->group_index);
      if (rc != 0)
        return -1;
    }
  }

  int rc = select_next_item(c, rt);

  if (rc != 0)
    return -1;
  rc = draw_prompt(c->session, rt->item_index);
  if (rc != 0)
    return -1;
  rc = log_prompt(c->session, rt->group_index, rt->item_index);
  if (rc != 0)
    return -1;
  return 0;
}

static int update_expiry(
    const struct ctx* c, struct runtime* rt, u64* remaining_ms) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(remaining_ms))
    return -1;
  if (!validate_ptr(c->session))
    return -1;
  if (!assert_ok(rt->group_index < c->session->group_count))
    return -1;

  *remaining_ms = 0;
  if (rt->pending_switch)
    return 0;

  u64 now = 0;
  int rc = now_ms(&now);

  if (rc != 0)
    return -1;
  if (now >= rt->group_end) {
    rt->pending_switch = 1;
    rc = log_group("expired", rt->group_index);
    if (rc != 0)
      return -1;
    return 0;
  }
  *remaining_ms = rt->group_end - now;
  return 0;
}

static int read_key(const struct ctx* c,
    const struct runtime* rt,
    u64 remaining_ms,
    int* key_out) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(key_out))
    return -1;
  if (!validate_ok(remaining_ms <= MAX_GROUP_MILLISECONDS))
    return -1;

  int timeout = rt->pending_switch ? -1 : (int)remaining_ms;
  int rc = term_read_key_timeout(timeout, key_out);

  if (rc < 0)
    return -1;
  return rc;
}

static int handle_key(
    const struct ctx* c, struct runtime* rt, int key, int* advanced) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(advanced))
    return -1;

  int rc = log_key(key);

  if (rc != 0)
    return -1;
  if (key == 3)
    return 1;
  if (!is_advance_key(key))
    return 0;

  if (rt->pending_switch) {
    rc = select_next_group(c, rt);
    if (rc != 0)
      return -1;
    rt->pending_switch = 0;
    rc = advance_prompt(c, rt, 1);
    if (rc != 0)
      return -1;
  } else {
    rc = advance_prompt(c, rt, 0);
    if (rc != 0)
      return -1;
  }
  *advanced = 1;
  return 0;
}

static int run_wait_loop(
    const struct ctx* c, struct runtime* rt, int* advanced) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(advanced))
    return -1;

  for (size_t wait = 0; wait < MAX_WAIT_LOOPS; wait++) {
    u64 remaining_ms = 0;
    int rc = update_expiry(c, rt, &remaining_ms);

    if (rc != 0)
      return -1;
    int key = 0;

    rc = read_key(c, rt, remaining_ms, &key);
    if (rc < 0)
      return -1;
    if (rc == 0)
      continue;
    int key_rc = handle_key(c, rt, key, advanced);

    if (key_rc < 0)
      return -1;
    if (key_rc > 0 || *advanced)
      return key_rc;
  }
  return -1;
}

static int run_loop(const struct ctx* c, struct runtime* rt) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(c->session))
    return -1;
  if (!assert_ok(c->session->group_count > 0))
    return -1;

  for (size_t step = 1; step < MAX_PROMPTS_PER_RUN; step++) {
    int advanced = 0;
    int rc = run_wait_loop(c, rt, &advanced);

    if (rc < 0)
      return -1;
    if (rc > 0)
      return 0;
    if (!advanced) {
      rc = log_simple("error", "wait loop exceeded");
      if (rc != 0)
        return -1;
      return -1;
    }
  }
  return 0;
}

static int init_runtime(const struct ctx* c, struct runtime* rt) {
  if (!validate_ptr(c))
    return -1;
  if (!validate_ptr(rt))
    return -1;
  if (!validate_ptr(c->session))
    return -1;
  if (!validate_ptr(c->rng))
    return -1;
  if (!assert_ok(c->session->group_count > 0))
    return -1;

  rt->order_pos = 0;
  rt->group_index = 0;
  rt->item_pos = 0;
  rt->item_index = 0;
  rt->group_end = 0;
  rt->pending_switch = 0;

  int rc = init_group_order(c);

  if (rc != 0)
    return -1;
  rc = rng_shuffle_groups(c->rng, c->group_order, c->session->group_count);
  if (rc != 0)
    return -1;
  rc = select_next_group(c, rt);
  if (rc != 0)
    return -1;
  rc = init_item_order(c, rt->group_index);
  if (rc != 0)
    return -1;

  struct Session* session = c->session;
  size_t group_index = rt->group_index;
  const struct Group* group = &session->groups[group_index];
  size_t count = group->item_count;

  if (!assert_ok(count > 0))
    return -1;
  if (!assert_ok(count <= MAX_ITEMS_PER_GROUP))
    return -1;
  rc = rng_shuffle_items(c->rng, c->item_order, count);
  if (rc != 0)
    return -1;
  rc = select_next_item(c, rt);
  if (rc != 0)
    return -1;
  rc = draw_prompt(c->session, rt->item_index);
  if (rc != 0)
    return -1;
  rc = log_prompt(c->session, rt->group_index, rt->item_index);
  if (rc != 0)
    return -1;
  rc = update_group_timer(c, rt);
  if (rc != 0)
    return -1;
  return 0;
}

int runner_run(const struct TermState* term,
    struct Session* session,
    struct Rng* rng,
    size_t* group_order,
    size_t* item_order) {
  if (!validate_ptr(term))
    return -1;
  if (!assert_ok(term->active == 1))
    return -1;
  if (!validate_ptr(session))
    return -1;
  if (assert_session_bounds(session) != 0)
    return -1;
  if (!validate_ptr(rng))
    return -1;
  if (!validate_ptr(group_order))
    return -1;
  if (!validate_ptr(item_order))
    return -1;

  struct ctx c = {
    .session = session,
    .rng = rng,
    .group_order = group_order,
    .item_order = item_order,
  };
  struct runtime rt;
  int rc = init_runtime(&c, &rt);

  if (rc != 0)
    return -1;
  rc = run_loop(&c, &rt);
  if (rc != 0)
    return -1;
  return 0;
}
