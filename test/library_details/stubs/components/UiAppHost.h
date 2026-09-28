#pragma once
#include "activities/Activity.h"
namespace fui = freeink::ui;
struct DetailsTarget : fui::DrawTarget {
  GfxRenderer& renderer;
  explicit DetailsTarget(const GfxRenderer& r) : renderer(const_cast<GfxRenderer&>(r)) {}
  fui::Size measureText(fui::FontId, const char* t, fui::TextStyle) const override {
    return {static_cast<int16_t>(strlen(t) * 6), 12};
  }
  int16_t lineHeight(fui::FontId) const override { return 12; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
  void text(fui::Rect r, const char* t, fui::TextStyle) override { renderer.lines.push_back({r, t}); }
};
struct UiAppHost {
  using UiApp = fui::FreeInkApp<24, 6>;
  using UiScreen = UiApp::ScreenType;
  DetailsTarget target;
  UiApp app;
  explicit UiAppHost(const GfxRenderer& r) : target(r), app(target, fui::DeviceContext{}) {}
  void resetUi() {}
  void renderUi() {
    app.setDevice(
        fui::DeviceContext{static_cast<int16_t>(target.renderer.width), static_cast<int16_t>(target.renderer.height)});
    app.render();
  }
};
