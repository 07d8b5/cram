#include "config.h"
#include "model.h"
#include "parser.h"
#include "rng.h"
#include "term.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct Runtime {
    struct Session *session;
    struct Rng *rng;
    size_t *group_order;
    size_t *item_order;
    size_t order_pos;
    size_t group_index;
    struct Group *group;
    size_t item_pos;
    size_t item_index;
    uint64_t group_end;
    int pending_switch;
    FILE *log;
};

static int now_ms(uint64_t *out_ms) {
    if (!out_ms) {
        return -1;
    }
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        *out_ms = 0;
        return -1;
    }
    *out_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
    return 0;
}

static int print_usage(const char *prog) {
    if (!prog) {
        return -1;
    }
    if (fprintf(stdout, "Usage: %s <session-file>\n", prog) < 0) {
        return -1;
    }
    if (fprintf(stdout, "       %s -h\n\n", prog) < 0) {
        return -1;
    }
    if (fprintf(stdout, "Keys: Enter / Space / alphanumeric = next prompt, Ctrl+C = quit\n") < 0) {
        return -1;
    }
    return 0;
}

static int log_write(FILE *log, const char *tag, const char *msg) {
    if (!log || !tag || !msg) {
        return -1;
    }
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return -1;
    }
    struct tm tm_val;
    if (!localtime_r(&ts.tv_sec, &tm_val)) {
        return -1;
    }
    char timebuf[32];
    size_t tlen = strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_val);
    if (tlen == 0) {
        return -1;
    }
    char line[256];
    int rc = snprintf(line, sizeof(line), "%s.%03ld [%s] %s\n", timebuf, ts.tv_nsec / 1000000L, tag, msg);
    if (rc < 0 || (size_t)rc >= sizeof(line)) {
        return -1;
    }
    size_t written = fwrite(line, 1, (size_t)rc, log);
    if (written != (size_t)rc) {
        return -1;
    }
    if (fflush(log) != 0) {
        return -1;
    }
    return 0;
}

static int log_simple(FILE *log, const char *tag, const char *msg) {
    if (!log || !tag || !msg) {
        return -1;
    }
    if (tag[0] == '\0') {
        return -1;
    }
    return log_write(log, tag, msg);
}

static int log_key(FILE *log, int key) {
    if (!log) {
        return -1;
    }
    char msg[64];
    int rc = snprintf(msg, sizeof(msg), "key=%d", key);
    if (rc < 0 || (size_t)rc >= sizeof(msg)) {
        return -1;
    }
    return log_write(log, "key", msg);
}

static int log_prompt(FILE *log, size_t group_index, size_t item_index) {
    if (!log) {
        return -1;
    }
    char msg[96];
    int rc = snprintf(msg, sizeof(msg), "group=%zu item=%zu", group_index, item_index);
    if (rc < 0 || (size_t)rc >= sizeof(msg)) {
        return -1;
    }
    return log_write(log, "prompt", msg);
}

static int log_group(FILE *log, const char *tag, size_t group_index) {
    if (!log || !tag) {
        return -1;
    }
    char msg[64];
    int rc = snprintf(msg, sizeof(msg), "group=%zu", group_index);
    if (rc < 0 || (size_t)rc >= sizeof(msg)) {
        return -1;
    }
    return log_write(log, tag, msg);
}

static int log_shuffle(FILE *log, const char *tag, size_t group_index) {
    if (!log || !tag) {
        return -1;
    }
    char msg[64];
    int rc = snprintf(msg, sizeof(msg), "group=%zu", group_index);
    if (rc < 0 || (size_t)rc >= sizeof(msg)) {
        return -1;
    }
    return log_write(log, tag, msg);
}

static int draw_prompt(const struct Session *session, const struct Item *item) {
    if (!session || !item) {
        return -1;
    }
    if (item->length == 0) {
        return -1;
    }
    if ((size_t)item->offset + (size_t)item->length > session->buffer_len) {
        return -1;
    }
    term_clear_screen();
    const char *text = session->buffer + item->offset;
    size_t written = fwrite(text, 1, item->length, stdout);
    if (written != item->length) {
        return -1;
    }
    if (fputc('\n', stdout) == EOF) {
        return -1;
    }
    if (fflush(stdout) != 0) {
        return -1;
    }
    return 0;
}

static int is_advance_key(int key) {
    if (key < 0) {
        return 0;
    }
    if (key == ' ' || key == '\r' || key == '\n') {
        return 1;
    }
    return isalnum((unsigned char)key) != 0;
}

static void init_group_order(const struct Session *session, size_t *order) {
    if (!session || !order) {
        return;
    }
    if (session->group_count > MAX_GROUPS) {
        return;
    }
    for (size_t i = 0; i < MAX_GROUPS; i++) {
        if (i < session->group_count) {
            order[i] = i;
        } else {
            order[i] = 0;
        }
    }
}

static void init_item_order(const struct Session *session, const struct Group *group, size_t *order) {
    if (!session || !group || !order) {
        return;
    }
    if (group->item_count > MAX_ITEMS_TOTAL) {
        return;
    }
    for (size_t i = 0; i < MAX_ITEMS_TOTAL; i++) {
        if (i < group->item_count) {
            order[i] = (size_t)group->item_start + i;
        } else {
            order[i] = 0;
        }
    }
}

static int select_next_group(struct Runtime *rt) {
    if (!rt || !rt->session || !rt->group_order || !rt->rng) {
        return -1;
    }
    if (rt->session->group_count == 0) {
        return -1;
    }
    if (rt->order_pos >= rt->session->group_count) {
        rng_shuffle_groups(rt->rng, rt->group_order, rt->session->group_count);
        rt->order_pos = 0;
        if (rt->log) {
            if (log_simple(rt->log, "shuffle", "groups") != 0) {
                return -1;
            }
        }
    }
    rt->group_index = rt->group_order[rt->order_pos];
    rt->order_pos++;
    rt->group = &rt->session->groups[rt->group_index];
    return 0;
}

static int select_next_item(struct Runtime *rt) {
    if (!rt || !rt->group || !rt->item_order) {
        return -1;
    }
    if (rt->group->item_count == 0) {
        return -1;
    }
    if (rt->item_pos >= rt->group->item_count) {
        rt->item_pos = 0;
    }
    rt->item_index = rt->item_order[rt->item_pos];
    return 0;
}

static int update_group_timer(struct Runtime *rt) {
    if (!rt || !rt->group) {
        return -1;
    }
    if (rt->group->seconds == 0) {
        return -1;
    }
    uint64_t now = 0;
    if (now_ms(&now) != 0) {
        rt->group_end = 0;
        return -1;
    }
    rt->group_end = now + (uint64_t)rt->group->seconds * 1000ULL;
    return 0;
}

static int advance_prompt(struct Runtime *rt, int due_to_switch) {
    if (!rt || !rt->session || !rt->group) {
        return -1;
    }
    if (rt->group->item_count == 0) {
        return -1;
    }
    if (due_to_switch) {
        init_item_order(rt->session, rt->group, rt->item_order);
        rng_shuffle_items(rt->rng, rt->item_order, rt->group->item_count);
        rt->item_pos = 0;
        if (update_group_timer(rt) != 0) {
            return -1;
        }
        if (rt->log) {
            if (log_group(rt->log, "group", rt->group_index) != 0) {
                return -1;
            }
        }
    } else {
        rt->item_pos++;
        if (rt->item_pos >= rt->group->item_count) {
            rng_shuffle_items(rt->rng, rt->item_order, rt->group->item_count);
            rt->item_pos = 0;
            if (rt->log) {
                if (log_shuffle(rt->log, "items", rt->group_index) != 0) {
                    return -1;
                }
            }
        }
    }
    if (select_next_item(rt) != 0) {
        return -1;
    }
    if (draw_prompt(rt->session, &rt->session->items[rt->item_index]) != 0) {
        return -1;
    }
    if (rt->log) {
        if (log_prompt(rt->log, rt->group_index, rt->item_index) != 0) {
            return -1;
        }
    }
    return 0;
}

static int update_expiry(struct Runtime *rt, uint64_t *remaining_ms) {
    if (!rt || !remaining_ms) {
        return -1;
    }
    if (!rt->group) {
        return -1;
    }
    *remaining_ms = 0;
    if (rt->pending_switch) {
        return 0;
    }
    uint64_t now = 0;
    if (now_ms(&now) != 0) {
        return -1;
    }
    if (now >= rt->group_end) {
        rt->pending_switch = 1;
        if (rt->log) {
            if (log_group(rt->log, "expired", rt->group_index) != 0) {
                return -1;
            }
        }
        return 0;
    }
    *remaining_ms = rt->group_end - now;
    return 0;
}

static int read_key(struct Runtime *rt, uint64_t remaining_ms, int *key_out) {
    if (!rt || !key_out) {
        return -1;
    }
    if (remaining_ms > 86400000ULL && !rt->pending_switch) {
        return -1;
    }
    int rc = term_read_key_timeout(rt->pending_switch ? -1 : (int)remaining_ms, key_out);
    if (rc < 0) {
        if (rt->log) {
            if (log_simple(rt->log, "error", "read input failed") != 0) {
                return -1;
            }
        }
        return -1;
    }
    return rc;
}

static int handle_key(struct Runtime *rt, int key, int *advanced) {
    if (!rt || !advanced) {
        return -1;
    }
    if (!rt->group) {
        return -1;
    }
    *advanced = 0;
    if (rt->log) {
        if (log_key(rt->log, key) != 0) {
            return -1;
        }
    }
    if (key == 3) {
        return 1;
    }
    if (!is_advance_key(key)) {
        return 0;
    }
    if (rt->pending_switch) {
        if (select_next_group(rt) != 0) {
            return -1;
        }
        rt->pending_switch = 0;
        if (advance_prompt(rt, 1) != 0) {
            return -1;
        }
    } else {
        if (advance_prompt(rt, 0) != 0) {
            return -1;
        }
    }
    *advanced = 1;
    return 0;
}

static int run_loop(struct Runtime *rt) {
    if (!rt) {
        return -1;
    }
    if (!rt->session) {
        return -1;
    }
    for (size_t step = 1; step < MAX_PROMPTS_PER_RUN; step++) {
        int advanced = 0;
        for (size_t wait = 0; wait < MAX_WAIT_LOOPS; wait++) {
            uint64_t remaining_ms = 0;
            if (update_expiry(rt, &remaining_ms) != 0) {
                return -1;
            }
            int key = 0;
            int rc = read_key(rt, remaining_ms, &key);
            if (rc < 0) {
                return -1;
            }
            if (rc == 0) {
                continue;
            }
            int key_rc = handle_key(rt, key, &advanced);
            if (key_rc < 0) {
                return -1;
            }
            if (key_rc > 0) {
                return 0;
            }
            if (advanced) {
                break;
            }
        }
        if (!advanced) {
            if (rt->log) {
                if (log_simple(rt->log, "error", "wait loop exceeded") != 0) {
                    return -1;
                }
            }
            return -1;
        }
    }
    return 0;
}

static int init_runtime(struct Runtime *rt, struct Session *session, struct Rng *rng, size_t *group_order, size_t *item_order, FILE *log) {
    if (!rt || !session || !rng || !group_order || !item_order) {
        return -1;
    }
    if (session->group_count == 0) {
        return -1;
    }
    rt->session = session;
    rt->rng = rng;
    rt->group_order = group_order;
    rt->item_order = item_order;
    rt->order_pos = 0;
    rt->group_index = 0;
    rt->group = NULL;
    rt->item_pos = 0;
    rt->item_index = 0;
    rt->group_end = 0;
    rt->pending_switch = 0;
    rt->log = log;

    init_group_order(session, group_order);
    rng_shuffle_groups(rng, group_order, session->group_count);
    if (select_next_group(rt) != 0) {
        return -1;
    }
    if (rt->group->item_count == 0) {
        return -1;
    }
    init_item_order(session, rt->group, item_order);
    rng_shuffle_items(rng, item_order, rt->group->item_count);
    rt->item_pos = 0;
    if (select_next_item(rt) != 0) {
        return -1;
    }
    if (draw_prompt(session, &session->items[rt->item_index]) != 0) {
        return -1;
    }
    if (log) {
        if (log_prompt(log, rt->group_index, rt->item_index) != 0) {
            return -1;
        }
    }
    if (update_group_timer(rt) != 0) {
        return -1;
    }
    return 0;
}

static int setup_session(const char *path, struct Session *session, char *err_buf, size_t err_len) {
    if (!path || !session || !err_buf || err_len == 0) {
        return -1;
    }
    if (path[0] == '\0') {
        return -1;
    }
    if (parse_session_file(path, session, err_buf, err_len) != 0) {
        if (fprintf(stderr, "Error: %s\n", err_buf) < 0) {
            return -1;
        }
        return -1;
    }
    return 0;
}

static FILE *open_log(void) {
    FILE *log = fopen("cram.log", "a");
    if (!log) {
        const char *err = strerror(errno);
        if (!err) {
            err = "unknown error";
        }
        if (fprintf(stderr, "Warning: failed to open cram.log: %s\n", err) < 0) {
            return NULL;
        }
        return NULL;
    }
    if (ferror(log)) {
        if (fclose(log) != 0) {
            return NULL;
        }
        return NULL;
    }
    if (log_simple(log, "start", "session started") != 0) {
        if (fclose(log) != 0) {
            return NULL;
        }
        return NULL;
    }
    return log;
}

static int run_with_terminal(struct Runtime *rt, char *err_buf, size_t err_len) {
    if (!rt || !err_buf || err_len == 0) {
        return -1;
    }
    struct TermState term = {0};
    if (term_enter_raw(&term, err_buf, err_len) != 0) {
        if (rt->log) {
            if (log_simple(rt->log, "error", "failed to enter raw mode") != 0) {
                return -1;
            }
        }
        if (fprintf(stderr, "Error: %s\n", err_buf) < 0) {
            return -1;
        }
        return -1;
    }
    term_hide_cursor();

    int rc = run_loop(rt);

    term_restore(&term);
    term_show_cursor();
    term_clear_screen();

    return rc;
}

static int close_log(FILE *log) {
    if (!log) {
        return 0;
    }
    if (log_simple(log, "exit", "session end") != 0) {
        return -1;
    }
    if (fclose(log) != 0) {
        return -1;
    }
    return 0;
}

static int run_program(const char *path) {
    if (!path) {
        return -1;
    }
    static struct Session session;
    char err_buf[256];
    if (setup_session(path, &session, err_buf, sizeof(err_buf)) != 0) {
        return -1;
    }

    FILE *log = open_log();

    struct Rng rng;
    rng_init(&rng);

    static size_t group_order[MAX_GROUPS];
    static size_t item_order[MAX_ITEMS_TOTAL];

    struct Runtime rt;
    if (init_runtime(&rt, &session, &rng, group_order, item_order, log) != 0) {
        if (log) {
            if (log_simple(log, "error", "failed to init runtime") != 0) {
                return -1;
            }
        }
        return -1;
    }

    int rc = run_with_terminal(&rt, err_buf, sizeof(err_buf));
    if (close_log(log) != 0) {
        return -1;
    }
    return rc;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        return print_usage(argv[0]) == 0 ? 0 : 1;
    }
    if (argc != 2) {
        return print_usage(argv[0]) == 0 ? 1 : 1;
    }
    return run_program(argv[1]) == 0 ? 0 : 1;
}
