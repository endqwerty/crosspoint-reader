#pragma once
#include "activities/Activity.h"
struct ButtonNavigator {
  inline static MappedInputManager* input = nullptr;
  struct Buttons {};
  static Buttons getNextButtons() { return {}; }
  template <class Fn>
  void onRelease(Buttons, Fn fn) {
    if (std::exchange(input->next, false)) fn();
  }
  template <class Fn>
  void onPreviousRelease(Fn fn) {
    if (std::exchange(input->previous, false)) fn();
  }
  template <class Fn>
  void onNextContinuous(Fn) {}
  template <class Fn>
  void onPreviousContinuous(Fn) {}
};
