#include "model.h"

int session_init(struct Session *session) {
    if (!session) {
        return -1;
    }
    session->buffer_len = 0;
    session->group_count = 0;
    session->item_count = 0;
    return 0;
}
