#include "term.h"

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static void write_all(const char *buf, size_t len) {
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < MAX_WRITE_LOOPS; i++) {
        if (len == 0) {
            break;
        }
        ssize_t n = write(STDOUT_FILENO, buf, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        buf += (size_t)n;
        len -= (size_t)n;
    }
}

int term_enter_raw(struct TermState *state, char *err_buf, size_t err_len) {
    if (!state || !err_buf || err_len == 0) {
        return -1;
    }
    if (tcgetattr(STDIN_FILENO, &state->original) != 0) {
        const char *err = strerror(errno);
        if (!err) {
            err = "unknown error";
        }
        int rc = snprintf(err_buf, err_len, "Failed to get terminal settings: %s", err);
        if (rc < 0) {
            return -1;
        }
        return -1;
    }

    struct termios raw = state->original;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        const char *err = strerror(errno);
        if (!err) {
            err = "unknown error";
        }
        int rc = snprintf(err_buf, err_len, "Failed to set terminal raw mode: %s", err);
        if (rc < 0) {
            return -1;
        }
        return -1;
    }

    state->active = 1;
    return 0;
}

void term_restore(struct TermState *state) {
    if (state && state->active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &state->original);
        state->active = 0;
    }
}

void term_clear_screen(void) {
    const char *seq = "\033[2J\033[H";
    size_t len = strlen(seq);
    if (len == 0) {
        return;
    }
    write_all(seq, len);
}

void term_hide_cursor(void) {
    const char *seq = "\033[?25l";
    size_t len = strlen(seq);
    if (len == 0) {
        return;
    }
    write_all(seq, len);
}

void term_show_cursor(void) {
    const char *seq = "\033[?25h";
    size_t len = strlen(seq);
    if (len == 0) {
        return;
    }
    write_all(seq, len);
}

int term_read_key_timeout(int timeout_ms, int *out_key) {
    if (!out_key) {
        return -1;
    }
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval tv;
    struct timeval *tv_ptr = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

    int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, tv_ptr);
    if (ready < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (ready == 0) {
        return 0;
    }

    unsigned char ch = 0;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n == 1) {
        *out_key = (int)ch;
        return 1;
    }
    if (n < 0 && errno == EINTR) {
        return 0;
    }
    return -1;
}
