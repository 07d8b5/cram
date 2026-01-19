// SPDX-License-Identifier: MIT
#include "term.h"

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static int write_all(const char* buf, size_t len) {
  if (!assert_ptr(buf))
    return -1;
  if (!assert_ok(len <= MAX_LINE_LEN))
    return -1;

  for (size_t i = 0; i < MAX_WRITE_LOOPS; i++) {
    if (len == 0)
      break;
    ssize_t n = write(STDOUT_FILENO, buf, len);

    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (n == 0)
      break;
    buf += (size_t)n;
    len -= (size_t)n;
  }
  if (len != 0)
    return -1;
  return 0;
}

int term_enter_raw(struct TermState* state, char* err_buf, size_t err_len) {
  if (!validate_ptr(state))
    return -1;
  if (!validate_ptr(err_buf))
    return -1;
  if (!validate_ok(err_len > 0))
    return -1;
  if (!assert_ok(state->active == 0 || state->active == 1))
    return -1;
  if (!assert_ok(state->active == 0))
    return -1;

  int rc = tcgetattr(STDIN_FILENO, &state->original);

  if (rc != 0) {
    const char* err = strerror(errno);

    if (!err)
      err = "unknown error";
    rc = snprintf(err_buf, err_len, "Failed to get terminal settings: %s", err);
    if (rc < 0)
      return -1;
    return -1;
  }

  struct termios raw = state->original;

  raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_oflag &= (tcflag_t) ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;

  rc = tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  if (rc != 0) {
    const char* err = strerror(errno);

    if (!err)
      err = "unknown error";
    rc = snprintf(err_buf, err_len, "Failed to set terminal raw mode: %s", err);
    if (rc < 0)
      return -1;
    return -1;
  }

  state->active = 1;
  return 0;
}

int term_restore(struct TermState* state) {
  if (!validate_ptr(state))
    return -1;
  if (!assert_ok(state->active == 0 || state->active == 1))
    return -1;
  if (!state->active)
    return 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &state->original) != 0) {
    state->active = 0;
    return -1;
  }
  state->active = 0;
  return 0;
}

int term_clear_screen(void) {
  const char* seq = "\033[2J\033[H";

  return write_all(seq, strlen(seq));
}

int term_hide_cursor(void) {
  const char* seq = "\033[?25l";

  return write_all(seq, strlen(seq));
}

int term_show_cursor(void) {
  const char* seq = "\033[?25h";

  return write_all(seq, strlen(seq));
}

int term_read_key_timeout(int timeout_ms, int* out_key) {
  if (!validate_ptr(out_key))
    return -1;
  if (!validate_ok(timeout_ms >= -1))
    return -1;

  fd_set readfds;

  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval tv;
  struct timeval* tv_ptr = NULL;

  if (timeout_ms >= 0) {
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    tv_ptr = &tv;
  }

  int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, tv_ptr);

  if (ready < 0) {
    if (errno == EINTR)
      return 0;
    return -1;
  }
  if (ready == 0)
    return 0;
  if (!assert_ok(ready == 1))
    return -1;
  if (!assert_ok(FD_ISSET(STDIN_FILENO, &readfds)))
    return -1;

  unsigned char ch = 0;
  ssize_t n = read(STDIN_FILENO, &ch, 1);

  if (n == 1) {
    *out_key = (int)ch;
    return 1;
  }
  if (n < 0 && errno == EINTR)
    return 0;
  return -1;
}
