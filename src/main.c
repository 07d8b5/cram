// SPDX-License-Identifier: MIT
#include "app.h"

int main(int argc, char** argv) {
  static struct app app;
  return app_main(&app, argc, argv);
}
