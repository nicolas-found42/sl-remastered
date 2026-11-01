#pragma once

#include <algorithm>
#include <cmath>
#include <istream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Only the input schedule pauses at a wait; simulation and loading keep running.
// wait splash observes the active splash cancel hotkey ready for normal Escape.
// clickn/moven use fractions of the current window (0..1); pixel verbs retain their meaning.
// click X Y queues both mouse edges at that position across animation passes.
class ProbeScript {
public:
  struct Action {
    double at = 0;
    std::string verb, a, b;
    double timeout = 0;
    size_t line = 0;
  };

  bool read(std::istream& input, std::string& error) {
    actions_.clear();
    next_ = 0;
    paused_ = 0;
    waitStarted_.reset();
    error.clear();
    size_t lineNumber = 0;
    for (std::string line; std::getline(input, line);) {
      ++lineNumber;
      std::istringstream fields(line);
      fields >> std::ws;
      if (fields.eof() || fields.peek() == '#')
        continue;
      Action action;
      action.line = lineNumber;
      if (!(fields >> action.at >> action.verb) || !std::isfinite(action.at) ||
          action.at < 0) {
        error = "invalid script time/action at line " + std::to_string(lineNumber);
        return false;
      }
      if (action.verb == "keydown" || action.verb == "keyup" ||
          action.verb == "sdl_keydown" || action.verb == "sdl_keyup" ||
          action.verb == "sdl_type") {
        std::getline(fields >> std::ws, action.a);
        while (!action.a.empty() &&
               (action.a.back() == ' ' || action.a.back() == '\r'))
          action.a.pop_back();
        if (action.a.empty()) {
          error = action.verb + " needs an argument at line " +
                  std::to_string(lineNumber);
          return false;
        }
      } else if (action.verb == "move" || action.verb == "sdl_move" ||
                 action.verb == "click" || action.verb == "clickn" || action.verb == "moven") {
        float x = 0, y = 0;
        if (!(fields >> x >> y) || !std::isfinite(x) || !std::isfinite(y)) {
          error = action.verb + " expects X Y at line " +
                  std::to_string(lineNumber);
          return false;
        }
        if ((action.verb == "clickn" || action.verb == "moven") && (x < 0 || x > 1 || y < 0 || y > 1)) {
          error = action.verb + " expects window fractions in [0,1] at line " + std::to_string(lineNumber);
          return false;
        }
        action.a = std::to_string(x);
        action.b = std::to_string(y);
      } else if (action.verb == "wait") {
        fields >> action.a >> action.b;
        std::istringstream timeout(action.b);
        if ((action.a != "splash" && action.a != "menu" && action.a != "options" && action.a != "video" && action.a != "driving") ||
            !(timeout >> action.timeout) || !std::isfinite(action.timeout) ||
            action.timeout <= 0 || !(timeout >> std::ws).eof()) {
          error = "expected wait splash|menu|options|video|driving <positive timeout> at line " +
                  std::to_string(lineNumber);
          return false;
        }
      } else if (action.verb == "shot" || action.verb == "fail") {
        if (!(fields >> action.a)) {
          error = action.verb + " needs an argument at line " +
                  std::to_string(lineNumber);
          return false;
        }
      } else if (action.verb != "down" && action.verb != "up" &&
                 action.verb != "sdl_down" && action.verb != "sdl_up" &&
                 action.verb != "quit") {
        error = "unknown action " + action.verb + " at line " +
                std::to_string(lineNumber);
        return false;
      }
      actions_.push_back(std::move(action));
    }
    if (input.bad()) {
      error = "cannot read script";
      return false;
    }
    std::stable_sort(actions_.begin(), actions_.end(),
                     [](const auto& a, const auto& b) { return a.at < b.at; });
    return true;
  }

  // A null result means not due or exhausted, unless error is nonempty. Returned
  // actions stay valid until read(); consume() acknowledges an executed action.
  // ready(condition) returns nullptr when ready, otherwise what still blocks it;
  // a timeout reports the last such reason.
  template <class Ready, class WaitComplete>
  const Action* poll(double now, Ready&& ready, WaitComplete&& complete,
                     std::string& error) {
    error.clear();
    while (next_ < actions_.size()) {
      const auto& action = actions_[next_];
      if (now - paused_ < action.at)
        return nullptr;
      if (action.verb != "wait")
        return &action;
      if (!waitStarted_)
        waitStarted_ = now;
      const double waited = now - *waitStarted_;
      // A late frame must not turn an expired wait into a successful run.
      if (waited >= action.timeout) {
        error = "wait " + action.a + " timed out at line " +
                std::to_string(action.line) + " (blocked by: " +
                (blockedBy_.empty() ? "not evaluated" : blockedBy_) + ")";
        return nullptr;
      }
      if (const char* blocked = ready(std::string_view(action.a))) {
        blockedBy_ = blocked;
        return nullptr;
      }
      // Resume the schedule at the wait's own offset even when the wait was
      // first reached late, so later input keeps its spacing.
      paused_ = now - action.at;
      complete(std::string_view(action.a), now, action.at, waited);
      waitStarted_.reset();
      blockedBy_.clear();
      ++next_;
    }
    return nullptr;
  }

  void consume() { ++next_; }

private:
  std::vector<Action> actions_;
  size_t next_ = 0;
  double paused_ = 0;
  std::optional<double> waitStarted_;
  std::string blockedBy_;
};
