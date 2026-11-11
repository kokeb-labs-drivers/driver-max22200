/**
 * @file port_without_delay.cpp
 * @brief Must NOT compile: tEN and tWU at start-up need the port's DelayUs().
 *
 * EXPECT: DelayUs
 */
#include "max22200.hpp"
#include "mock_max22200.hpp"

class NoDelayPort : public max22200_test::MockPortBase<NoDelayPort> {};

int main() {
  NoDelayPort port;
  max22200::MAX22200<NoDelayPort> chip(port);
  return static_cast<int>(chip.EnsureInitialized().error);
}
