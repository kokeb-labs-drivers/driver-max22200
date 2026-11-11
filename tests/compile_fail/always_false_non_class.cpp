/**
 * @file always_false_non_class.cpp
 * @brief Must NOT compile: `detail::kAlwaysFalse` depends on a class (the port or another class of
 * the discarded branch), so `int` does not satisfy its constraint (handbook KB-CPP20-005, -008).
 *
 * EXPECT: ClassType
 */
#include "max22200_detail.hpp"

static_assert(!max22200::detail::kAlwaysFalse<int>);

int main() {
  return 0;
}
