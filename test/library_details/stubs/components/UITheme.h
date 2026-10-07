#pragma once
#include "activities/Activity.h"
struct ThemeMetrics {
  int topPadding = 4, headerHeight = 32, buttonHintsHeight = 24;
};
struct UITheme {
  ThemeMetrics metrics;
  static UITheme& getInstance();
  const ThemeMetrics& getMetrics() const { return metrics; }
  static Rect getContentArea(const GfxRenderer&);
  Rect getScreenSafeArea(const GfxRenderer&, bool, bool);
};
inline UITheme themeInstance;
inline UITheme& UITheme::getInstance() { return themeInstance; }
struct DrawingTheme {
  std::string header;
  void drawHeader(GfxRenderer&, Rect, const char* t) { header = t; }
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) {}
};
inline DrawingTheme GUI;
