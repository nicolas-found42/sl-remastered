#include "../tools/probe_script.hpp"

#include <cstdio>
#include <sstream>
#include <string>

static int failures = 0;
#define CHECK(condition)                                       \
  do {                                                         \
    if (!(condition)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition); \
      ++failures;                                              \
    }                                                          \
  } while (0)

int main() {
  std::string error;
  const auto ignore = [](std::string_view, double, double, double) {};
  const auto never = [](std::string_view) -> const char* { return "loading"; };
  const auto always = [](std::string_view) -> const char* { return nullptr; };
  {
    ProbeScript script;
    std::istringstream input("0 wait menu 30\n0 down\n0.25 up\n0.25 wait driving 60\n2.25 quit\n");
    CHECK(script.read(input, error));
    bool ready = false;
    const auto predicate = [&](std::string_view) -> const char* {
      return ready ? nullptr : "not ready";
    };
    CHECK(!script.poll(0, predicate, ignore, error));
    CHECK(!script.poll(4, predicate, ignore, error));
    ready = true;
    auto* action = script.poll(5, predicate, ignore, error);
    CHECK(action && action->verb == "down");
    script.consume();
    CHECK(!script.poll(5.125, predicate, ignore, error));
    action = script.poll(5.25, predicate, ignore, error);
    CHECK(action && action->verb == "up");
    script.consume();
    ready = false;
    CHECK(!script.poll(5.25, predicate, ignore, error));
    ready = true;
    CHECK(!script.poll(15.25, predicate, ignore, error));
    CHECK(!script.poll(17, predicate, ignore, error));
    action = script.poll(17.25, predicate, ignore, error);
    CHECK(action && action->verb == "quit");
    CHECK(error.empty());
  }
  {
    // A wait first reached late (slow frame) still keeps the following
    // keydown/keyup two seconds apart instead of releasing them together.
    ProbeScript script;
    std::istringstream input("1 wait driving 10\n1 keydown Up\n3 keyup Up\n");
    CHECK(script.read(input, error));
    double logical = -1;
    const auto record = [&](std::string_view, double, double at, double) { logical = at; };
    auto* action = script.poll(2.5, always, record, error);
    CHECK(action && action->verb == "keydown");
    CHECK(logical == 1);
    script.consume();
    CHECK(!script.poll(4.4, always, record, error));
    action = script.poll(4.5, always, record, error);
    CHECK(action && action->verb == "keyup");
  }
  {
    ProbeScript script;
    std::istringstream input("0 wait menu 2\n0 quit\n");
    CHECK(script.read(input, error));
    CHECK(!script.poll(10, never, ignore, error));
    CHECK(error.empty());
    CHECK(!script.poll(12, always, ignore, error));
    CHECK(error.find("timed out at line 1") != std::string::npos);
    CHECK(error.find("loading") != std::string::npos);
  }
  {
    ProbeScript script;
    std::istringstream input("# old absolute schedules still run in time order\n2 keyup Left Shift\n1 keydown Left Shift\n");
    CHECK(script.read(input, error));
    CHECK(!script.poll(0.5, never, ignore, error));
    auto* action = script.poll(1, never, ignore, error);
    CHECK(action && action->verb == "keydown" && action->a == "Left Shift");
    script.consume();
    CHECK(!script.poll(1.5, never, ignore, error));
    action = script.poll(2, never, ignore, error);
    CHECK(action && action->verb == "keyup" && action->a == "Left Shift");
  }
  {
    // Predetermined navigation runs at the same schedule offset, while the
    // real splash and menu independently report readiness.
    ProbeScript script;
    std::istringstream input("0 wait splash 30\n0 keydown Escape\n0 wait menu 30\n0 keyup Escape\n0 click 420 127\n0 wait driving 60\n0.1 keydown Up\n2.1 keyup Up\n");
    CHECK(script.read(input, error));
    std::string active = "loading";
    const auto ready = [&](std::string_view condition) -> const char* {
      return condition == active ? nullptr : "wrong active state";
    };
    CHECK(!script.poll(0, ready, ignore, error));
    active = "splash";
    auto* action = script.poll(0.01, ready, ignore, error);
    CHECK(action && action->verb == "keydown");
    script.consume();
    CHECK(!script.poll(0.01, ready, ignore, error));
    active = "menu";
    action = script.poll(0.02, ready, ignore, error);
    CHECK(action && action->verb == "keyup");
    script.consume();
    action = script.poll(0.02, ready, ignore, error);
    CHECK(action && action->verb == "click" && std::stof(action->a) == 420 && std::stof(action->b) == 127);
    script.consume();
    CHECK(!script.poll(0.02, ready, ignore, error));
    active = "driving";
    CHECK(!script.poll(4, ready, ignore, error));
    action = script.poll(4.11, ready, ignore, error);
    CHECK(action && action->verb == "keydown");
    script.consume();
    CHECK(!script.poll(6, ready, ignore, error));
    action = script.poll(6.11, ready, ignore, error);
    CHECK(action && action->verb == "keyup");
  }
  {
    ProbeScript script;
    std::istringstream input("0 clickn 0.25 0.75\n0 moven 1 0\n");
    CHECK(script.read(input, error));
    const auto* action = script.poll(0, always, ignore, error);
    CHECK(action && action->verb == "clickn" && std::stof(action->a) == 0.25f && std::stof(action->b) == 0.75f);
    script.consume();
    action = script.poll(0, always, ignore, error);
    CHECK(action && action->verb == "moven" && std::stof(action->a) == 1 && std::stof(action->b) == 0);
  }
  for (const auto* text : {"0 wait unknown 2\n", "0 wait menu 0\n",
                           "0 wait menu -1\n", "0 wait driving 2oops\n",
                           "bad wait menu 2\n", "0 press Up\n", "0 keydown\n",
                           "0 clickn -0.1 0.5\n", "0 moven 0.5 1.1\n", "0 clickn 0.5\n", "0 click 10\n", "0 click nan 4\n", "0 wait splash 0\n", "0 move 10\n", "0 move x 4\n", "0 shot\n"}) {
    ProbeScript script;
    std::istringstream input(text);
    CHECK(!script.read(input, error));
    CHECK(!error.empty());
  }
  return failures ? 1 : 0;
}
