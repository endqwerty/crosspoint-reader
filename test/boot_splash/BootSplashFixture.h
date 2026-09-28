#pragma once
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include "fontIds.h"
#include "images/Logo120.h"

extern HalDisplay display;
struct RenderLock {
  inline static unsigned held = 0;
  inline static unsigned acquisitions = 0;
  RenderLock() {
    EXPECT_EQ(held, 0u);
    ++held;
    ++acquisitions;
  }
  ~RenderLock() { --held; }
};
struct Activity {
  unsigned enters = 0;
  void onEnter() { ++enters; }
};
struct BootActivity : Activity {
  GfxRenderer& renderer;
  explicit BootActivity(GfxRenderer& renderer) : renderer(renderer) {}
  void onEnter();
};
struct SettingsFixture {
  uint8_t screenInverted = 0;
};
inline SettingsFixture SETTINGS;
enum StrId { STR_CROSSPOINT, STR_BOOTING };
inline const char* tr(StrId id) { return id == STR_CROSSPOINT ? "CrossPoint" : "Booting..."; }
#define CROSSPOINT_VERSION "boot-test"
