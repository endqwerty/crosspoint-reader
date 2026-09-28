#pragma once
#include <Print.h>

#include <algorithm>
#include <string>
#include <vector>

#include "TestUi.h"
class Epub {
 public:
  struct Item {
    std::string href;
  };
  std::vector<std::string> chapters;
  mutable unsigned reads = 0;
  bool readFailure = false;
  int getSpineItemsCount() const { return static_cast<int>(chapters.size()); }
  Item getSpineItem(int index) const { return {std::to_string(index)}; }
  bool readItemContentsToStream(const std::string& href, Print& out, size_t chunk, bool early) const {
    EXPECT_TRUE(GfxRenderer::loanActive);
    EXPECT_TRUE(searchTestRenderLockHeld);
    struct ReadScope {
      ReadScope() {
        EXPECT_FALSE(searchTestReadActive);
        searchTestReadActive = true;
      }
      ~ReadScope() { searchTestReadActive = false; }
    } readScope;
    reads++;
    const auto& text = chapters.at(static_cast<size_t>(std::stoi(href)));
    for (size_t offset = 0; offset < text.size();) {
      const auto size = std::min(chunk, text.size() - offset);
      if (out.write(reinterpret_cast<const uint8_t*>(text.data() + offset), size) != size) return early;
      offset += size;
    }
    return !readFailure;
  }
};
