// SPDX-License-Identifier: MIT
#include "app.h"
#include "log.h"
#include "parser.h"
#include "runner.h"
#include "term.h"

#include <stdio.h>
#include <string.h>

static int print_usage(const char* prog) {
  if (!prog)
    return -1;

  int rc = fprintf(stdout, "Usage: %s <session-file>\n", prog);

  if (rc < 0)
    return -1;
  rc = fprintf(stdout, "       %s -h\n\n", prog);
  if (rc < 0)
    return -1;
  rc = fprintf(stdout, "Keys: Enter/Space/alnum = next, Ctrl+C = quit\n");
  if (rc < 0)
    return -1;
  return 0;
}

static int setup_session(struct app* app, const char* path) {
  if (!validate_ptr(app))
    return -1;
  if (!validate_ptr(path))
    return -1;
  if (!validate_ok(path[0] != '\0'))
    return -1;

  char err_buf[256];
  int rc = parse_session_file(path, &app->session, err_buf, sizeof(err_buf));

  if (rc != 0) {
    rc = fprintf(stderr, "Error: %s\n", err_buf);
    if (rc < 0)
      return -1;
    return -1;
  }
  return 0;
}

int app_main(struct app* app, int argc, char** argv) {
  if (!validate_ptr(app))
    return 1;
  if (!validate_ptr(argv))
    return 1;
  if (!validate_ok(argc >= 0))
    return 1;

  if (argc == 2 &&
      (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
    int rc = print_usage(argv[0]);

    return (rc == 0) ? 0 : 1;
  }
  if (argc != 2) {
    int rc = print_usage(argv[0]);

    /* Usage error.
     * If printing usage fails, signal a distinct failure.
     */
    return (rc == 0) ? 1 : 2;
  }

  return (app_run_file(app, argv[1]) == 0) ? 0 : 1;
}

static int run_with_terminal(struct app* app) {
  char err_buf[256];

  int rc = term_enter_raw(&app->term, err_buf, sizeof(err_buf));

  if (rc != 0) {
    rc = fprintf(stderr, "Error: %s\n", err_buf);
    if (rc < 0)
      return -1;
    return -1;
  }

  int hide_rc = term_hide_cursor();
  int loop_rc = -1;

  if (hide_rc == 0) {
    loop_rc = runner_run(&app->term,
        &app->session,
        &app->rng,
        app->group_order,
        app->item_order);
  }

  int restore_rc = term_restore(&app->term);
  int show_rc = term_show_cursor();
  int clear_rc = term_clear_screen();

  if (!assert_ok(restore_rc == 0))
    return -1;
  if (!assert_ok(show_rc == 0))
    return -1;
  if (!assert_ok(clear_rc == 0))
    return -1;

  if (hide_rc != 0)
    return -1;
  return loop_rc;
}

int app_run_file(struct app* app, const char* path) {
  if (!validate_ptr(app))
    return -1;
  if (!validate_ptr(path))
    return -1;

  int rc = setup_session(app, path);

  if (rc != 0)
    return -1;
  rc = log_open(&app->session);
  if (rc != 0)
    return -1;
  rc = log_input(&app->session, path);
  if (rc != 0)
    return -1;
  rc = rng_init(&app->rng);
  if (rc != 0)
    return -1;

  rc = run_with_terminal(app);
  if (rc != 0)
    return -1;
  rc = log_close(&app->session);
  if (rc != 0)
    return -1;
  return 0;
}
