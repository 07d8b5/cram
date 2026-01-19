#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int set_error(char *err_buf, size_t err_len, const char *msg) {
    if (!err_buf || err_len == 0 || !msg) {
        return -1;
    }
    int rc = snprintf(err_buf, err_len, "%s", msg);
    if (rc < 0) {
        return -1;
    }
    return -1;
}

static int set_error_line(char *err_buf, size_t err_len, size_t line_no, const char *msg) {
    if (!err_buf || err_len == 0 || !msg) {
        return -1;
    }
    int rc = snprintf(err_buf, err_len, "Line %zu: %s", line_no, msg);
    if (rc < 0) {
        return -1;
    }
    return -1;
}

static size_t trim_left_index(const char *line, size_t line_len) {
    if (!line) {
        return line_len;
    }
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < MAX_LINE_LEN; i++) {
        if (i >= line_len) {
            break;
        }
        if (!isspace((unsigned char)line[i])) {
            start = i;
            found = 1;
            break;
        }
    }
    if (!found) {
        return line_len;
    }
    return start;
}

static size_t trim_right_index(const char *line, size_t line_len, size_t start) {
    if (!line) {
        return start;
    }
    size_t end = line_len;
    for (size_t i = 0; i < MAX_LINE_LEN; i++) {
        if (line_len == 0 || end == start) {
            break;
        }
        if (end == 0) {
            break;
        }
        if (!isspace((unsigned char)line[end - 1])) {
            break;
        }
        end--;
    }
    return end;
}

static int is_blank_or_comment(const char *line, size_t line_len) {
    if (!line) {
        return 1;
    }
    size_t start = trim_left_index(line, line_len);
    if (start >= line_len) {
        return 1;
    }
    return line[start] == '#';
}

static int parse_header_line(struct Session *session, char *line, size_t line_len, size_t line_no, char *err_buf, size_t err_len) {
    if (!session || !line) {
        return set_error_line(err_buf, err_len, line_no, "invalid header state");
    }
    if (line_len < 3 || line[0] != '[' || line[line_len - 1] != ']') {
        return set_error_line(err_buf, err_len, line_no, "malformed header");
    }

    size_t pipe_index = 0;
    int pipe_found = 0;
    for (size_t i = 1; i < MAX_LINE_LEN; i++) {
        if (i >= line_len - 1) {
            break;
        }
        if (line[i] == '|') {
            pipe_index = i;
            pipe_found = 1;
            break;
        }
    }
    if (!pipe_found) {
        return set_error_line(err_buf, err_len, line_no, "malformed header");
    }

    line[line_len - 1] = '\0';
    line[pipe_index] = '\0';

    char *name = line + 1;
    size_t name_len = pipe_index - 1;
    size_t name_start = trim_left_index(name, name_len);
    size_t name_end = trim_right_index(name, name_len, name_start);
    if (name_start >= name_end) {
        return set_error_line(err_buf, err_len, line_no, "malformed header");
    }
    name[name_end] = '\0';
    name += name_start;

    char *sec = line + pipe_index + 1;
    size_t sec_len = (line_len - 1) - (pipe_index + 1);
    size_t sec_start = trim_left_index(sec, sec_len);
    size_t sec_end = trim_right_index(sec, sec_len, sec_start);
    if (sec_start >= sec_end) {
        return set_error_line(err_buf, err_len, line_no, "malformed header");
    }
    sec[sec_end] = '\0';
    sec += sec_start;

    errno = 0;
    char *endptr = NULL;
    unsigned long secs = strtoul(sec, &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0' || secs < 1 || secs > 86400) {
        return set_error_line(err_buf, err_len, line_no, "invalid seconds value");
    }
    if (session->group_count >= MAX_GROUPS) {
        return set_error_line(err_buf, err_len, line_no, "too many groups");
    }

    struct Group *group = &session->groups[session->group_count];
    group->name_offset = (uint32_t)(name - session->buffer);
    size_t name_length = strlen(name);
    if (name_length > MAX_LINE_LEN) {
        return set_error_line(err_buf, err_len, line_no, "group name too long");
    }
    group->name_length = (uint32_t)name_length;
    group->seconds = (uint32_t)secs;
    group->item_start = (uint32_t)session->item_count;
    group->item_count = 0;
    session->group_count++;
    return 0;
}

int parse_session_file(const char *path, struct Session *session, char *err_buf, size_t err_len) {
    if (!path || !session) {
        return set_error(err_buf, err_len, "invalid arguments");
    }
    if (session_init(session) != 0) {
        return set_error(err_buf, err_len, "failed to init session");
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        char msg[256];
        const char *err = strerror(errno);
        if (!err) {
            err = "unknown error";
        }
        int rc = snprintf(msg, sizeof(msg), "Failed to open '%s': %s", path, err);
        if (rc < 0 || (size_t)rc >= sizeof(msg)) {
            return set_error(err_buf, err_len, "failed to open file");
        }
        return set_error(err_buf, err_len, msg);
    }

    size_t nread = fread(session->buffer, 1, MAX_FILE_BYTES, fp);
    if (ferror(fp)) {
        fclose(fp);
        return set_error(err_buf, err_len, "failed to read file");
    }
    int extra = fgetc(fp);
    if (extra != EOF) {
        fclose(fp);
        return set_error(err_buf, err_len, "file exceeds MAX_FILE_BYTES");
    }
    if (fclose(fp) != 0) {
        return set_error(err_buf, err_len, "failed to close file");
    }

    session->buffer_len = nread;
    session->buffer[nread] = '\0';

    size_t line_start = 0;
    size_t line_no = 1;
    struct Group *current = NULL;

    for (size_t i = 0; i <= MAX_FILE_BYTES; i++) {
        if (i == session->buffer_len || session->buffer[i] == '\n') {
            size_t line_len = i - line_start;
            if (line_len > 0 && session->buffer[line_start + line_len - 1] == '\r') {
                line_len--;
            }
            if (line_len > MAX_LINE_LEN) {
                return set_error_line(err_buf, err_len, line_no, "line too long");
            }
            char *line = &session->buffer[line_start];
            line[line_len] = '\0';

            if (!is_blank_or_comment(line, line_len)) {
                if (line[0] == '[') {
                    if (current && current->item_count == 0) {
                        return set_error_line(err_buf, err_len, line_no, "previous group has no items");
                    }
                    if (parse_header_line(session, line, line_len, line_no, err_buf, err_len) != 0) {
                        return -1;
                    }
                    current = &session->groups[session->group_count - 1];
                } else {
                    if (!current) {
                        return set_error_line(err_buf, err_len, line_no, "item before any group header");
                    }
                    if (session->item_count >= MAX_ITEMS_TOTAL) {
                        return set_error_line(err_buf, err_len, line_no, "too many items");
                    }
                    struct Item *item = &session->items[session->item_count];
                    item->offset = (uint32_t)line_start;
                    item->length = (uint32_t)line_len;
                    session->item_count++;
                    current->item_count++;
                }
            }

            line_start = i + 1;
            line_no++;
            if (i == session->buffer_len) {
                break;
            }
        }
    }

    if (session->group_count == 0) {
        return set_error(err_buf, err_len, "no groups found");
    }
    if (current && current->item_count == 0) {
        return set_error_line(err_buf, err_len, line_no, "last group has no items");
    }

    return 0;
}
