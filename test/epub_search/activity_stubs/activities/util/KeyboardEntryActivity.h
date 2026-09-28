#pragma once
#include "TestUi.h"
enum class InputType { Text };
class KeyboardEntryActivity : public Activity {
 public:
  KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& input, std::string, std::string, size_t, InputType)
      : Activity(renderer, input) {}
};
