#ifndef CRAM_PARSER_H
#define CRAM_PARSER_H

#include "model.h"

int parse_session_file(const char *path, struct Session *session, char *err_buf, size_t err_len);

#endif
