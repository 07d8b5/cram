// SPDX-License-Identifier: MIT
#include "log.h"
#include "config.h"
#include "model.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_log_fd = -1;

static u32 cksum_update(u32 crc, unsigned char b) {
  crc ^= (u32)b << 24;
  for (int i = 0; i < 8; i++) {
    if (crc & 0x80000000U)
      crc = (crc << 1) ^ 0x04C11DB7U;
    else
      crc <<= 1;
  }
  return crc;
}

static int cksum_bytes(u32* out, const unsigned char* buf, size_t len) {
  if (!validate_ptr(out))
    return -1;
  if (!validate_ptr(buf))
    return -1;
  if (!assert_ok(len <= MAX_FILE_BYTES))
    return -1;

  u32 crc = 0;

  for (size_t i = 0; i < MAX_FILE_BYTES; i++) {
    if (i >= len)
      break;
    crc = cksum_update(crc, buf[i]);
  }

  size_t n = len;

  for (size_t i = 0; i < sizeof(size_t); i++) {
    if (n == 0)
      break;
    crc = cksum_update(crc, (unsigned char)(n & 0xFF));
    n >>= 8;
  }
  *out = ~crc;
  return 0;
}

static size_t sanitize_path(const char* path, char* out, size_t out_len) {
  if (!validate_ptr(out))
    return 0;
  if (!out_len)
    return 0;
  if (!path) {
    out[0] = '\0';
    return 0;
  }

  size_t j = 0;

  for (size_t i = 0; i < MAX_LINE_LEN; i++) {
    if (j + 1 >= out_len)
      break;
    char ch = path[i];
    if (ch == '\0')
      break;

    if (ch == '\n' || ch == '\r')
      ch = ' ';
    out[j++] = ch;
  }
  out[j] = '\0';
  return j;
}

static size_t sanitize_abs_path(const char* path, char* out, size_t out_len) {
  if (!out_len)
    return 0;
  if (!path || path[0] == '\0') {
    out[0] = '\0';
    return 0;
  }

  if (path[0] == '/')
    return sanitize_path(path, out, out_len);

  char cwd[256];

  if (!getcwd(cwd, sizeof(cwd)))
    return sanitize_path(path, out, out_len);

  char abs_buf[384];
  int rc = snprintf(abs_buf, sizeof(abs_buf), "%s/%s", cwd, path);

  if (rc < 0 || (size_t)rc >= sizeof(abs_buf))
    return sanitize_path(path, out, out_len);
  return sanitize_path(abs_buf, out, out_len);
}

static int write_all_fd(int fd, const char* buf, size_t len) {
  if (!validate_ok(fd >= 0))
    return -1;
  if (!validate_ptr(buf))
    return -1;
  if (!validate_ok(len <= MAX_LINE_LEN))
    return -1;

  const char* ptr = buf;
  size_t remaining = len;

  for (size_t i = 0; i < MAX_WRITE_LOOPS; i++) {
    if (remaining == 0)
      break;
    ssize_t n = write(fd, ptr, remaining);

    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (n == 0)
      break;
    ptr += (size_t)n;
    remaining -= (size_t)n;
  }
  if (remaining != 0)
    return -1;
  return 0;
}

static int log_write(const char* tag, const char* msg) {
  if (!validate_ptr(tag))
    return -1;
  if (!validate_ptr(msg))
    return -1;
  if (!assert_ok(g_log_fd >= 0))
    return -1;

  struct timespec ts;
  int rc = clock_gettime(CLOCK_REALTIME, &ts);

  if (rc != 0)
    return -1;

  u64 sec = (u64)ts.tv_sec;
  u64 ms = (u64)(ts.tv_nsec / 1000000L);
  char line[256];

  rc = snprintf(line,
      sizeof(line),
      "%llu.%03llu [%s] %s\n",
      (unsigned long long)sec,
      (unsigned long long)ms,
      tag,
      msg);
  if (!assert_ok(rc > 0))
    return -1;
  if (!assert_ok((size_t)rc < sizeof(line)))
    return -1;

  char* ptr = line;
  size_t len = (size_t)rc;

  for (size_t i = 0; i < MAX_WRITE_LOOPS; i++) {
    ssize_t n = write(g_log_fd, ptr, len);

    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (n == 0)
      break;
    ptr += (size_t)n;
    len -= (size_t)n;
    if (len == 0)
      break;
  }
  if (len != 0)
    return -1;
  return 0;
}

int log_simple(const char* tag, const char* msg) {
  if (!validate_ptr(tag))
    return -1;
  if (!validate_ptr(msg))
    return -1;
  if (g_log_fd < 0)
    return 0;
  return log_write(tag, msg);
}

int log_key(int key) {
  if (!validate_ok(key >= 0))
    return -1;
  if (!validate_ok(key <= 255))
    return -1;
  if (g_log_fd < 0)
    return 0;

  char msg[64];
  int rc = snprintf(msg, sizeof(msg), "key=%d", key);

  if (!assert_ok(rc > 0))
    return -1;
  if (!assert_ok((size_t)rc < sizeof(msg)))
    return -1;
  return log_write("key", msg);
}

int log_prompt(
    const struct Session* session, size_t group_index, size_t item_index) {
  if (!validate_ptr(session))
    return -1;
  if (!assert_ok(group_index < session->group_count))
    return -1;
  if (!assert_ok(item_index < session->item_count))
    return -1;
  if (!assert_ok(group_index < MAX_GROUPS))
    return -1;
  if (!assert_ok(item_index < MAX_ITEMS_TOTAL))
    return -1;
  if (g_log_fd < 0)
    return 0;

  const struct Group* group = &session->groups[group_index];
  const struct Item* item = &session->items[item_index];
  const char* buf = session->buffer;
  u32 group_name_offset = group->name_offset;
  u32 group_name_length = group->name_length;
  u32 item_offset = item->offset;
  u32 item_length = item->length;

  size_t group_name_end = (size_t)group_name_offset + (size_t)group_name_length;

  if (!assert_ok(group_name_end <= session->buffer_len))
    return -1;

  size_t item_end = (size_t)item_offset + (size_t)item_length;

  if (!assert_ok(item_end <= session->buffer_len))
    return -1;

  const unsigned char* gname = (const unsigned char*)&buf[group_name_offset];
  const unsigned char* ibytes = (const unsigned char*)&buf[item_offset];

  u32 gck = 0;
  int rc = cksum_bytes(&gck, gname, (size_t)group_name_length);
  if (rc != 0)
    return -1;
  u32 ick = 0;
  rc = cksum_bytes(&ick, ibytes, (size_t)item_length);
  if (rc != 0)
    return -1;

  char msg[96];
  rc = snprintf(msg,
      sizeof(msg),
      "group=%zu item=%zu gck=%u glen=%u ick=%u ilen=%u",
      group_index,
      item_index,
      gck,
      (unsigned int)group_name_length,
      ick,
      (unsigned int)item_length);
  if (!assert_ok(rc > 0))
    return -1;
  if (!assert_ok((size_t)rc < sizeof(msg)))
    return -1;
  return log_write("prompt", msg);
}

int log_group(const char* tag, size_t group_index) {
  if (!validate_ptr(tag))
    return -1;
  if (!assert_ok(group_index < MAX_GROUPS))
    return -1;
  if (g_log_fd < 0)
    return 0;

  char msg[64];
  int rc = snprintf(msg, sizeof(msg), "group=%zu", group_index);

  if (!assert_ok(rc > 0))
    return -1;
  if (!assert_ok((size_t)rc < sizeof(msg)))
    return -1;
  return log_write(tag, msg);
}

int log_shuffle(const char* tag, size_t group_index) {
  if (!validate_ptr(tag))
    return -1;
  if (!assert_ok(group_index < MAX_GROUPS))
    return -1;
  if (g_log_fd < 0)
    return 0;

  char msg[64];
  int rc = snprintf(msg, sizeof(msg), "group=%zu", group_index);

  if (!assert_ok(rc > 0))
    return -1;
  if (!assert_ok((size_t)rc < sizeof(msg)))
    return -1;
  return log_write(tag, msg);
}

int log_input(const struct Session* session, const char* path) {
  if (!validate_ptr(session))
    return -1;
  if (g_log_fd < 0)
    return 0;
  if (!assert_ok(session->buffer_len <= MAX_FILE_BYTES))
    return -1;
  if (!assert_ok(session->buffer[session->buffer_len] == '\0'))
    return -1;

  u32 ck = 0;
  int rc = cksum_bytes(
      &ck, (const unsigned char*)session->buffer, session->buffer_len);
  if (rc != 0)
    return -1;

  char safe_path[192];
  size_t have_path = sanitize_abs_path(path, safe_path, sizeof(safe_path));

  char msg[256];
  rc = 0;

  if (have_path) {
    rc = snprintf(msg,
        sizeof(msg),
        "cksum=%u len=%zu path=%s",
        ck,
        session->buffer_len,
        safe_path);
  } else {
    rc =
        snprintf(msg, sizeof(msg), "cksum=%u len=%zu", ck, session->buffer_len);
  }
  if (rc < 0 || (size_t)rc >= sizeof(msg))
    return -1;
  return log_write("file", msg);
}

int log_open(const struct Session* session) {
  if (!validate_ptr(session))
    return -1;
  if (!assert_ok(session->group_count <= MAX_GROUPS))
    return -1;
  if (!assert_ok(session->item_count <= MAX_ITEMS_TOTAL))
    return -1;

  g_log_fd = open("cram.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (g_log_fd < 0) {
    const char* err = strerror(errno);

    if (!err)
      err = "unknown error";
    char msg[256];
    int rc = snprintf(
        msg, sizeof(msg), "Warning: failed to open cram.log: %s\n", err);
    if (rc < 0 || (size_t)rc >= sizeof(msg))
      return -1;
    int wrc = write_all_fd(STDERR_FILENO, msg, (size_t)rc);

    if (wrc != 0)
      return -1;
    return 0;
  }
  return log_simple("start", "session started");
}

int log_close(const struct Session* session) {
  if (!validate_ptr(session))
    return -1;
  if (!assert_ok(session->group_count <= MAX_GROUPS))
    return -1;
  if (!assert_ok(session->item_count <= MAX_ITEMS_TOTAL))
    return -1;
  if (g_log_fd < 0)
    return 0;

  int rc = log_simple("exit", "session end");

  if (rc != 0)
    return -1;
  rc = close(g_log_fd);
  if (rc != 0)
    return -1;
  g_log_fd = -1;
  return 0;
}
