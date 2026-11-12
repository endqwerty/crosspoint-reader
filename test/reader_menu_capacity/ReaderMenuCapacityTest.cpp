#include <gtest/gtest.h>

#include "MenuItemsProduction.h"

// The menu's rows live in a fixed array of MAX_MENU_ITEMS; a longer menu would hand the list
// more rows than the array holds.
TEST(ReaderMenuCapacity, EveryOptionalEntryFitsTheRowStorage) {
  std::vector<EpubReaderMenuActivity::MenuItem> items;
  EpubReaderMenuActivity::buildMenuItems(items, true, true, true);
  EXPECT_LE(items.size(), EpubReaderMenuActivity::MAX_MENU_ITEMS);
}

TEST(ReaderMenuCapacity, OptionalEntriesAreLeftOutWhenUnavailable) {
  Frontlight.fitted = false;
  bleturner::statusValue.compiledIn = false;
  SETTINGS.dictionaryName[0] = '\0';
  KOREADER_STORE.credentials = false;
  std::vector<EpubReaderMenuActivity::MenuItem> all;
  std::vector<EpubReaderMenuActivity::MenuItem> fewest;
  EpubReaderMenuActivity::buildMenuItems(fewest, false, false, false);
  Frontlight.fitted = true;
  bleturner::statusValue.compiledIn = true;
  SETTINGS.dictionaryName[0] = 'd';
  KOREADER_STORE.credentials = true;
  EpubReaderMenuActivity::buildMenuItems(all, true, true, true);
  // Footnotes, bookmarks, clippings, light, Bluetooth, lookup and sync.
  EXPECT_EQ(all.size() - fewest.size(), 7u);
}
