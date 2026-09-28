#pragma once

#include <I18n.h>

#include "activities/UiListActivity.h"

// Short Library pickers share the existing list/touch/navigation conventions.
class LibraryMenuActivity final : public UiListActivity {
 public:
  LibraryMenuActivity(GfxRenderer& renderer, MappedInputManager& input, const std::string& title, const StrId* labels,
                      int count);

 private:
  static constexpr int CAPACITY = 8;
  StrId labels[CAPACITY]{};
  freeink::ui::ListItem rows[CAPACITY]{};
  std::string title;
  int count;
  int listCount() const override { return count; }
  const char* headerTitle() const override { return title.c_str(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
};
