#ifndef CRAM_TERM_H
#define CRAM_TERM_H

#include <stddef.h>
#include <termios.h>

struct TermState {
    struct termios original;
    int active;
};

int term_enter_raw(struct TermState *state, char *err_buf, size_t err_len);
void term_restore(struct TermState *state);
void term_clear_screen(void);
void term_hide_cursor(void);
void term_show_cursor(void);

int term_read_key_timeout(int timeout_ms, int *out_key);

#endif
