// SPDX-License-Identifier: MIT
#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct parse_state {
  size_t line_no;
  int has_group;
  size_t current_group;
};

static int set_error(char* err_buf, size_t err_len, const char* msg) {
  if (!validate_ptr(err_buf))
    return -1;
  if (!validate_ok(err_len > 0))
    return -1;
  if (!validate_ptr(msg))
    return -1;

  int rc = snprintf(err_buf, err_len, "%s", msg);

  if (rc < 0)
    return -1;
  return -1;
}

static int set_error_line(
    char* err_buf, size_t err_len, size_t line_no, const char* msg) {
  if (!validate_ptr(err_buf))
    return -1;
  if (!validate_ok(err_len > 0))
    return -1;
  if (!validate_ptr(msg))
    return -1;

  int rc = snprintf(err_buf, err_len, "Line %zu: %s", line_no, msg);

  if (rc < 0)
    return -1;
  return -1;
}

static size_t trim_left_index(const char* line, size_t line_len) {
  if (!validate_ptr(line))
    return line_len;
  if (!validate_ok(line_len <= MAX_LINE_LEN))
    return line_len;

  for (size_t i = 0; i < MAX_LINE_LEN; i++) {
    if (i >= line_len)
      break;
    if (!isspace((unsigned char)line[i]))
      return i;
  }
  return line_len;
}

static size_t trim_right_index(
    const char* line, size_t line_len, size_t start) {
  if (!validate_ptr(line))
    return start;
  if (!validate_ok(line_len <= MAX_LINE_LEN))
    return start;

  size_t end = line_len;

  for (size_t i = 0; i < MAX_LINE_LEN; i++) {
    if (i >= line_len)
      break;
    if (end <= start || end == 0)
      break;
    if (!isspace((unsigned char)line[end - 1]))
      break;
    end--;
  }
  return end;
}

static int is_blank_or_comment(const char* line, size_t line_len) {
  if (!validate_ptr(line))
    return 1;
  if (!validate_ok(line_len <= MAX_LINE_LEN))
    return 1;

  size_t start = trim_left_index(line, line_len);

  if (start >= line_len)
    return 1;
  return line[start] == '#';
}

static int find_pipe_index(
    const char* line, size_t line_len, size_t* out_index) {
  if (!validate_ptr(line))
    return -1;
  if (!validate_ptr(out_index))
    return -1;

  for (size_t i = 1; i + 1 < line_len && i < MAX_LINE_LEN; i++) {
    if (line[i] == '|') {
      *out_index = i;
      return 0;
    }
  }
  return -1;
}

static int parse_seconds_value(const char* sec,
    size_t line_no,
    char* err_buf,
    size_t err_len,
    unsigned int* out_seconds) {
  if (!validate_ptr(sec))
    return -1;
  if (!validate_ptr(out_seconds))
    return -1;

  errno = 0;
  char* endptr = NULL;
  unsigned long secs = strtoul(sec, &endptr, 10);

  if (errno != 0 || !endptr || *endptr != '\0' || secs < 1 ||
      secs > MAX_GROUP_SECONDS)
    return set_error_line(err_buf, err_len, line_no, "invalid seconds value");
  *out_seconds = (unsigned int)secs;
  return 0;
}

static int parse_header_line(struct Session* session,
    char* line,
    size_t line_len,
    size_t line_no,
    char* err_buf,
    size_t err_len) {
  if (!validate_ptr(session))
    return -1;
  if (!validate_ptr(line))
    return -1;
  if (!validate_ok(line_len <= MAX_LINE_LEN))
    return -1;

  if (line_len < 3 || line[0] != '[' || line[line_len - 1] != ']')
    return set_error_line(err_buf, err_len, line_no, "malformed header");

  size_t pipe_index = 0;
  int rc = find_pipe_index(line, line_len, &pipe_index);

  if (rc != 0)
    return set_error_line(err_buf, err_len, line_no, "malformed header");

  line[line_len - 1] = '\0';
  line[pipe_index] = '\0';

  char* name = line + 1;
  size_t name_len = pipe_index - 1;
  size_t name_start = trim_left_index(name, name_len);
  size_t name_end = trim_right_index(name, name_len, name_start);

  if (name_start >= name_end)
    return set_error_line(err_buf, err_len, line_no, "malformed header");
  name[name_end] = '\0';
  name += name_start;

  char* sec = line + pipe_index + 1;
  size_t sec_len = (line_len - 1) - (pipe_index + 1);
  size_t sec_start = trim_left_index(sec, sec_len);
  size_t sec_end = trim_right_index(sec, sec_len, sec_start);

  if (sec_start >= sec_end)
    return set_error_line(err_buf, err_len, line_no, "malformed header");
  sec[sec_end] = '\0';
  sec += sec_start;

  unsigned int seconds = 0;

  rc = parse_seconds_value(sec, line_no, err_buf, err_len, &seconds);
  if (rc != 0)
    return -1;
  if (session->group_count >= MAX_GROUPS)
    return set_error_line(err_buf, err_len, line_no, "too many groups");

  struct Group* group = &session->groups[session->group_count];
  size_t name_length = strlen(name);

  if (name_length > MAX_LINE_LEN)
    return set_error_line(err_buf, err_len, line_no, "group name too long");
  group->name_offset = (u32)(name - session->buffer);
  group->name_length = (u32)name_length;
  group->seconds = (u32)seconds;
  group->item_start = (u32)session->item_count;
  group->item_count = 0;
  session->group_count++;
  return 0;
}

static int parse_item_line(struct Session* session,
    struct parse_state* state,
    size_t line_start,
    size_t line_len,
    char* err_buf,
    size_t err_len) {
  if (!validate_ptr(session))
    return -1;
  if (!validate_ptr(state))
    return -1;

  if (!state->has_group)
    return set_error_line(
        err_buf, err_len, state->line_no, "item before any group header");
  if (session->item_count >= MAX_ITEMS_TOTAL)
    return set_error_line(err_buf, err_len, state->line_no, "too many items");
  struct Group* group = &session->groups[state->current_group];

  if (group->item_count >= MAX_ITEMS_PER_GROUP)
    return set_error_line(
        err_buf, err_len, state->line_no, "too many items in group");

  struct Item* item = &session->items[session->item_count];

  item->offset = (u32)line_start;
  item->length = (u32)line_len;
  session->item_count++;
  group->item_count++;
  return 0;
}

static int handle_line(struct Session* session,
    struct parse_state* state,
    char* line,
    size_t line_len,
    size_t line_start,
    char* err_buf,
    size_t err_len) {
  if (!validate_ptr(session))
    return -1;
  if (!validate_ptr(state))
    return -1;
  if (!validate_ptr(line))
    return -1;

  if (is_blank_or_comment(line, line_len))
    return 0;
  if (line[0] == '[') {
    if (state->has_group) {
      const struct Group* group = &session->groups[state->current_group];
      if (group->item_count == 0)
        return set_error_line(
            err_buf, err_len, state->line_no, "previous group has no items");
    }
    int rc = parse_header_line(
        session, line, line_len, state->line_no, err_buf, err_len);
    if (rc != 0)
      return -1;
    state->current_group = session->group_count - 1;
    state->has_group = 1;
    return 0;
  }
  return parse_item_line(
      session, state, line_start, line_len, err_buf, err_len);
}

static int parse_session_buffer(
    struct Session* session, char* err_buf, size_t err_len) {
  if (!validate_ptr(session))
    return -1;
  if (!validate_ptr(err_buf))
    return -1;
  if (!validate_ok(err_len > 0))
    return -1;

  struct parse_state state;

  state.line_no = 1;
  state.has_group = 0;
  state.current_group = 0;

  size_t line_start = 0;

  for (size_t i = 0; i <= MAX_FILE_BYTES; i++) {
    if (i == session->buffer_len || session->buffer[i] == '\n') {
      size_t line_len = i - line_start;

      if (line_len > 0 && session->buffer[line_start + line_len - 1] == '\r')
        line_len--;
      if (line_len > MAX_LINE_LEN)
        return set_error_line(err_buf, err_len, state.line_no, "line too long");
      char* line = &session->buffer[line_start];

      line[line_len] = '\0';
      int rc = handle_line(
          session, &state, line, line_len, line_start, err_buf, err_len);
      if (rc != 0)
        return -1;
      line_start = i + 1;
      state.line_no++;
      if (i == session->buffer_len)
        break;
    }
  }

  if (session->group_count == 0)
    return set_error(err_buf, err_len, "no groups found");
  if (state.has_group) {
    const struct Group* group = &session->groups[state.current_group];
    if (group->item_count == 0)
      return set_error_line(
          err_buf, err_len, state.line_no, "last group has no items");
  }
  return 0;
}

static int read_file_into_session(
    const char* path, struct Session* session, char* err_buf, size_t err_len) {
  if (!validate_ptr(path))
    return -1;
  if (!validate_ptr(session))
    return -1;
  if (!validate_ptr(err_buf))
    return -1;
  if (!validate_ok(err_len > 0))
    return -1;

  FILE* fp = fopen(path, "rb");

  if (!fp) {
    const char* err = strerror(errno);

    if (!err)
      err = "unknown error";
    char msg[256];
    int rc = snprintf(msg, sizeof(msg), "Failed to open '%s': %s", path, err);
    if (rc < 0 || (size_t)rc >= sizeof(msg))
      return set_error(err_buf, err_len, "failed to open file");
    return set_error(err_buf, err_len, msg);
  }

  size_t nread = fread(session->buffer, 1, MAX_FILE_BYTES, fp);

  if (ferror(fp)) {
    int crc = fclose(fp);

    if (crc != 0)
      return set_error(err_buf, err_len, "failed to close file");
    return set_error(err_buf, err_len, "failed to read file");
  }
  int extra = fgetc(fp);

  if (extra != EOF) {
    int crc = fclose(fp);

    if (crc != 0)
      return set_error(err_buf, err_len, "failed to close file");
    return set_error(err_buf, err_len, "file exceeds MAX_FILE_BYTES");
  }
  if (fclose(fp) != 0)
    return set_error(err_buf, err_len, "failed to close file");

  session->buffer_len = nread;
  session->buffer[nread] = '\0';
  return 0;
}

int parse_session_file(
    const char* path, struct Session* session, char* err_buf, size_t err_len) {
  if (!validate_ptr(path))
    return -1;
  if (!validate_ptr(session))
    return -1;
  if (!validate_ptr(err_buf))
    return -1;
  if (!validate_ok(err_len > 0))
    return -1;

  int rc = session_init(session);

  if (rc != 0)
    return set_error(err_buf, err_len, "failed to init session");
  rc = read_file_into_session(path, session, err_buf, err_len);
  if (rc != 0)
    return -1;
  rc = parse_session_buffer(session, err_buf, err_len);
  if (rc != 0)
    return -1;
  return 0;
}
