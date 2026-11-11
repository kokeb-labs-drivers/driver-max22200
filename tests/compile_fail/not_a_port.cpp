/**
 * @file not_a_port.cpp
 * @brief Must NOT compile: the driver is constrained by the port concept.
 *
 * EXPECT: Port
 */
#include "max22200.hpp"

struct NotAPort {
  bool EnsureInitialized() noexcept {
    return true;
  }
};

int main() {
  NotAPort port;
  max22200::MAX22200<NotAPort> chip(port);
  return static_cast<int>(chip.EnsureInitialized().error);
}
