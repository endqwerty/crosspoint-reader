#pragma once

namespace BoardConfig {
struct ViewableInsets {
  int top = 0, right = 0, bottom = 0, left = 0;
};
inline struct {
  ViewableInsets viewableInsets;
} ACTIVE;
}  // namespace BoardConfig
