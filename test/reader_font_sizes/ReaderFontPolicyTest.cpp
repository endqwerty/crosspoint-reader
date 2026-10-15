#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <cstring>
#include <iterator>

#include "ReaderFontSizes.h"
#include "fontIds.h"

// Only the fields used by the extracted production methods are available here.
class CrossPointSettings {
 public:
#include "ReaderFontPolicyEnums.inc"
  uint8_t fontFamily = NOTOSERIF;
  uint8_t fontPointSize = 14;
  uint8_t lineSpacing = NORMAL;
  char sdFontFamilyName[32] = "";
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;
  int getReaderFontId() const;
  float getReaderLineCompression() const;
  bool loadFontPolicy(const JsonDocument& doc) {
    auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };
    bool needsResave = false;
#include "ReaderFontFamilyJson.inc"
#include "ReaderFontSdJson.inc"
    return needsResave;
  }
};

#include "ReaderFontPolicyProduction.inc"

const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string&) const { return nullptr; }
std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const { return {}; }

namespace {
constexpr uint8_t SIZES[] = {12, 14, 16, 18};
constexpr int SERIF_IDS[] = {LIBRON_12_FONT_ID, LIBRON_14_FONT_ID, LIBRON_16_FONT_ID, LIBRON_18_FONT_ID};
constexpr int SANS_IDS[] = {NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID};
constexpr int SD_FONT_ID = 4242;

int resolveInstalled(void*, const char* familyName, uint8_t) {
  return std::strcmp(familyName, "Literata") == 0 ? SD_FONT_ID : 0;
}
}  // namespace

TEST(ReaderFontPolicy, SerifSlotRendersLibronAndSansSlotRendersNotoSans) {
  for (size_t i = 0; i < std::size(SIZES); ++i) {
    CrossPointSettings settings;
    settings.fontPointSize = SIZES[i];
    settings.fontFamily = CrossPointSettings::NOTOSERIF;
    EXPECT_EQ(settings.getReaderFontId(), SERIF_IDS[i]);
    settings.fontFamily = CrossPointSettings::NOTOSANS;
    EXPECT_EQ(settings.getReaderFontId(), SANS_IDS[i]);
    // An out-of-range family renders the serif slot.
    settings.fontFamily = 7;
    EXPECT_EQ(settings.getReaderFontId(), SERIF_IDS[i]);
  }
}

TEST(ReaderFontPolicy, UnsupportedSizesSnapAndTiesChooseSmaller) {
  for (unsigned pt = 0; pt <= 255; ++pt) {
    CrossPointSettings settings;
    settings.fontPointSize = static_cast<uint8_t>(pt);
    const size_t index = pt <= 13 ? 0 : pt <= 15 ? 1 : pt <= 17 ? 2 : 3;
    EXPECT_EQ(settings.getReaderFontId(), SERIF_IDS[index]) << pt;
  }
}

TEST(ReaderFontPolicy, InstalledSdFontIsUsedAndMissingOneFallsBackToBuiltin) {
  CrossPointSettings settings;
  settings.fontFamily = CrossPointSettings::NOTOSANS;
  settings.sdFontIdResolver = resolveInstalled;
  std::strcpy(settings.sdFontFamilyName, "Literata");
  EXPECT_EQ(settings.getReaderFontId(), SD_FONT_ID);
  std::strcpy(settings.sdFontFamilyName, "Bookerly");
  EXPECT_EQ(settings.getReaderFontId(), NOTOSANS_14_FONT_ID);
}

TEST(ReaderFontPolicy, ForkLibronValueMigratesOnceToTheSerifSlot) {
  CrossPointSettings settings;
  JsonDocument doc;
  doc["fontFamily"] = 2;  // the fork's saved Libron value before this change
  doc["sdFontFamilyName"] = "";
  EXPECT_TRUE(settings.loadFontPolicy(doc));
  EXPECT_EQ(settings.fontFamily, CrossPointSettings::NOTOSERIF);
  EXPECT_EQ(settings.sdFontFamilyName[0], '\0');
  EXPECT_EQ(settings.getReaderFontId(), LIBRON_14_FONT_ID);
  doc["fontFamily"] = settings.fontFamily;
  EXPECT_FALSE(settings.loadFontPolicy(doc));
}

TEST(ReaderFontPolicy, SavedBuiltinAndSdChoicesArePreserved) {
  for (const uint8_t family : {CrossPointSettings::NOTOSERIF, CrossPointSettings::NOTOSANS}) {
    for (const char* sdFamily : {"", "Literata", "Bookerly"}) {
      CrossPointSettings settings;
      JsonDocument doc;
      doc["fontFamily"] = family;
      doc["sdFontFamilyName"] = sdFamily;
      EXPECT_FALSE(settings.loadFontPolicy(doc));
      EXPECT_EQ(settings.fontFamily, family);
      EXPECT_STREQ(settings.sdFontFamilyName, sdFamily);
    }
  }
}

TEST(ReaderFontPolicy, UnknownSavedFamilyResetsToSerifSlot) {
  CrossPointSettings settings;
  JsonDocument doc;
  doc["fontFamily"] = 9;
  EXPECT_TRUE(settings.loadFontPolicy(doc));
  EXPECT_EQ(settings.fontFamily, CrossPointSettings::NOTOSERIF);
}

TEST(ReaderFontPolicy, LineSpacingFollowsTheActiveFamily) {
  constexpr float SERIF[] = {0.95f, 1.0f, 1.1f, 1.2f};
  constexpr float SANS[] = {0.90f, 0.95f, 1.0f, 1.05f};
  constexpr float SD[] = {0.95f, 1.0f, 1.3f, 1.6f};
  for (uint8_t spacing = 0; spacing < std::size(SERIF); ++spacing) {
    CrossPointSettings settings;
    settings.lineSpacing = spacing;
    EXPECT_FLOAT_EQ(settings.getReaderLineCompression(), SERIF[spacing]);
    settings.fontFamily = CrossPointSettings::NOTOSANS;
    EXPECT_FLOAT_EQ(settings.getReaderLineCompression(), SANS[spacing]);
    std::strcpy(settings.sdFontFamilyName, "Literata");
    EXPECT_FLOAT_EQ(settings.getReaderLineCompression(), SD[spacing]);
  }
}

TEST(ReaderFontPolicy, RegisteredBuiltinIdsAreUniqueAndNonZero) {
  for (size_t i = 0; i < std::size(SIZES); ++i) {
    EXPECT_NE(SERIF_IDS[i], 0);
    EXPECT_NE(SANS_IDS[i], 0);
    for (size_t j = 0; j < std::size(SIZES); ++j) {
      EXPECT_NE(SERIF_IDS[i], SANS_IDS[j]);
      if (i != j) EXPECT_NE(SERIF_IDS[i], SERIF_IDS[j]);
    }
  }
}
