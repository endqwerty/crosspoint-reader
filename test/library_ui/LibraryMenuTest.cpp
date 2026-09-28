#include <gtest/gtest.h>

#include "MenuUiFixture.h"

TEST(LibraryMenu, ClampsRowsAndRejectsInvalidActivation) {
  GfxRenderer renderer;
  MappedInputManager input;
  std::array<StrId, 12> labels{};
  LibraryMenuActivity menu(renderer, input, "Menu", labels.data(), 12);
  EXPECT_EQ(menu.count, 8);
  menu.activateIndex(-1);
  menu.activateIndex(8);
  EXPECT_EQ(menu.finished, 0);
  menu.activateIndex(7);
  EXPECT_EQ(menu.finished, 1);
  EXPECT_EQ(menu.result.action, 7);
  LibraryMenuActivity empty(renderer, input, "Empty", nullptr, -2);
  EXPECT_EQ(empty.count, 0);
  empty.activateIndex(0);
  EXPECT_EQ(empty.finished, 0);
}
TEST(LibraryMenu, BackIsExplicitCancellation) {
  GfxRenderer renderer;
  MappedInputManager input;
  std::array<StrId, 1> labels{};
  LibraryMenuActivity menu(renderer, input, "Menu", labels.data(), 1);
  menu.onBackButton();
  EXPECT_EQ(menu.finished, 1);
  EXPECT_TRUE(menu.result.isCancelled);
}
TEST(LibraryMenu, TouchRowsAndSafeInsetsFollowScreenGeometry) {
  MappedInputManager input;
  std::array<StrId, 3> labels{};
  for (const auto dimensions : {std::pair{480, 800}, std::pair{800, 480}}) {
    GfxRenderer renderer;
    renderer.width = dimensions.first;
    renderer.height = dimensions.second;
    LibraryMenuActivity menu(renderer, input, "Menu", labels.data(), 3);
    UiScreen screen;
    menu.buildScreen(screen);
    EXPECT_EQ(menu.viewportSyncs, 1);
    EXPECT_EQ(screen.margins.top, 50);
    EXPECT_EQ(screen.margins.right, 17);
    EXPECT_EQ(screen.margins.bottom, 19);
    EXPECT_EQ(screen.margins.left, 11);
    EXPECT_EQ(screen.props.count, 3);
    EXPECT_EQ(screen.props.inputMask, freeink::ui::InputTouch);
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(screen.props.items[i].actionValue, i);
      EXPECT_NE(screen.props.items[i].label, nullptr);
    }
  }
}
