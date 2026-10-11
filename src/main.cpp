#include <cstdlib>
#include <exception>

#include "app.hpp"

int main(int argc, char **argv) {
  auto app = core::make_ref<app_t>(argc, argv);
  try {
    app->run();
  } catch (const std::exception &e) {
    horizon_error("unhandled exception: {}", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
