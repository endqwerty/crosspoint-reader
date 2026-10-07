#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <cstring>
#include <iterator>

#include "ReaderFontSizes.h"
#include "fontIds.h"

// Only the fields used by the extracted production methods are available here.
// In particular there is no SD/vector font resolver to call in the render getter.
class CrossPointSettings {
 public:
#include "ReaderFontPolicyEnums.inc"
  uint8_t fontFamily = LIBRON;
  uint8_t fontPointSize = 14;
  uint8_t lineSpacing = NORMAL;
  uint8_t paragraphAlignment = 3;
  uint8_t wordSpacing = 97;
  char sdFontFamilyName[32] = "";
  bool enforceReaderFont();
  int getReaderFontId() const;
  float getReaderLineCompression() const;
  bool loadFontPolicy(const JsonDocument& doc) {
    bool needsResave = false;
    const uint8_t storedFontSize = doc["fontSize"] | 14;
    fontPointSize = storedFontSize;
#include "ReaderFontJsonPolicy.inc"
    return needsResave;
  }
};

#include "ReaderFontPolicyProduction.inc"

const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string&) const { return nullptr; }
std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const { return {}; }

namespace {
constexpr uint8_t SIZES[] = {12, 14, 16, 18};
constexpr int IDS[] = {LIBRON_12_FONT_ID, LIBRON_14_FONT_ID, LIBRON_16_FONT_ID, LIBRON_18_FONT_ID};
constexpr int OLD_IDS[] = {NOTOSERIF_12_FONT_ID, NOTOSERIF_14_FONT_ID, NOTOSERIF_16_FONT_ID, NOTOSERIF_18_FONT_ID,
                           NOTOSANS_12_FONT_ID,  NOTOSANS_14_FONT_ID,  NOTOSANS_16_FONT_ID,  NOTOSANS_18_FONT_ID};
}  // namespace

TEST(ReaderFontPolicy, OverridesBuiltinAndSdChoicesAtEverySupportedSize) {
  for (const uint8_t family : {0, 1, 2, 255}) {
    for (size_t i = 0; i < std::size(SIZES); ++i) {
      CrossPointSettings settings;
      settings.fontFamily = family;
      settings.fontPointSize = SIZES[i];
      std::strcpy(settings.sdFontFamilyName, "InstalledVectorOrBitmap");
      EXPECT_EQ(settings.getReaderFontId(), IDS[i]);
      EXPECT_TRUE(settings.enforceReaderFont());
      EXPECT_EQ(settings.fontFamily, CrossPointSettings::LIBRON);
      EXPECT_EQ(settings.sdFontFamilyName[0], '\0');
      EXPECT_EQ(settings.fontPointSize, SIZES[i]);
      EXPECT_FALSE(settings.enforceReaderFont());
      EXPECT_EQ(settings.paragraphAlignment, 3);
      EXPECT_EQ(settings.wordSpacing, 97);
    }
  }
}

TEST(ReaderFontPolicy, InvalidAndVectorSizesSnapAndTiesChooseSmaller) {
  for (unsigned pt = 0; pt <= 255; ++pt) {
    CrossPointSettings settings;
    settings.fontPointSize = pt;
    const size_t index = pt <= 13 ? 0 : pt <= 15 ? 1 : pt <= 17 ? 2 : 3;
    EXPECT_EQ(settings.getReaderFontId(), IDS[index]) << pt;
    EXPECT_EQ(settings.enforceReaderFont(), pt != SIZES[index]) << pt;
    EXPECT_EQ(settings.fontPointSize, SIZES[index]) << pt;
    EXPECT_FALSE(settings.enforceReaderFont());
  }
}

TEST(ReaderFontPolicy, SavedFamiliesTriggerOneMigrationAndThenRemainCanonical) {
  for (const uint8_t family : {0, 1, 2, 255}) {
    for (const char* sdFamily : {"", "Vector", "Bitmap"}) {
      CrossPointSettings settings;
      JsonDocument doc;
      doc["fontFamily"] = family;
      doc["fontSize"] = 17;
      doc["sdFontFamilyName"] = sdFamily;
      EXPECT_TRUE(settings.loadFontPolicy(doc));
      EXPECT_EQ(settings.fontPointSize, 16);
      EXPECT_EQ(settings.getReaderFontId(), LIBRON_16_FONT_ID);
      doc["fontFamily"] = settings.fontFamily;
      doc["fontSize"] = settings.fontPointSize;
      doc["sdFontFamilyName"] = settings.sdFontFamilyName;
      EXPECT_FALSE(settings.loadFontPolicy(doc));
    }
  }
}

TEST(ReaderFontPolicy, CanonicalSavedSizesDoNotRequestResave) {
  for (const uint8_t size : SIZES) {
    CrossPointSettings settings;
    JsonDocument doc;
    doc["fontFamily"] = CrossPointSettings::LIBRON;
    doc["fontSize"] = size;
    doc["sdFontFamilyName"] = "";
    EXPECT_FALSE(settings.loadFontPolicy(doc));
    EXPECT_EQ(settings.fontPointSize, size);
  }
}

TEST(ReaderFontPolicy, CompressionPreservesSpacingRegardlessOfSavedFamily) {
  constexpr float expected[] = {0.95f, 1.0f, 1.1f, 1.2f};
  CrossPointSettings settings;
  settings.fontFamily = CrossPointSettings::NOTOSANS;
  std::strcpy(settings.sdFontFamilyName, "Vector");
  for (uint8_t spacing = 0; spacing < std::size(expected); ++spacing) {
    settings.lineSpacing = spacing;
    EXPECT_FLOAT_EQ(settings.getReaderLineCompression(), expected[spacing]);
    settings.enforceReaderFont();
    EXPECT_EQ(settings.lineSpacing, spacing);
    EXPECT_FLOAT_EQ(settings.getReaderLineCompression(), expected[spacing]);
  }
  settings.lineSpacing = 255;
  EXPECT_FLOAT_EQ(settings.getReaderLineCompression(), 1.0f);
}

TEST(ReaderFontPolicy, FontIdsInvalidateOldLayoutsAndAreUnique) {
  for (size_t i = 0; i < std::size(IDS); ++i) {
    EXPECT_NE(IDS[i], 0);
    for (const int oldId : OLD_IDS) EXPECT_NE(IDS[i], oldId);
    for (size_t j = i + 1; j < std::size(IDS); ++j) EXPECT_NE(IDS[i], IDS[j]);
  }
}
