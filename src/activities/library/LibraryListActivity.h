#pragma once

#include <LibraryBookState.h>
#include <LibraryIndexFile.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "activities/UiTabListActivity.h"

// Recent, arrival and title shelves, plus author/series directories whose rows
// open only that group's books. Rows use the shared UiTabListActivity ring
// (0 = the tab strip, 1..N = rows) for both buttons and touch.
//
// Only the visible window of rows is materialized per render (strings and
// ListItems for at most one page). The ordinary shelf therefore keeps one page
// of strings; an active search additionally uses one fallible uint16_t slot per
// indexed book so an allocation failure remains recoverable on the C3.
class LibraryListActivity final : public UiTabListActivity {
 public:
  LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void onExit() override;

 protected:
  // --- UiListActivity / UiTabListActivity contract ---------------------------
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  int tabCount() const override;
  int activeTab() const override;
  const char* tabLabel(int index) const override;
  freeink::ui::TabIndicator tabIndicator(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void navigateButtons() override;
  // The FreeInkUI header owns both the title and search touch target.
  void drawChrome() override {}
  void drawFooter() override;

 private:
  // The screen's own actions, after the base's ACTION_ROW / ACTION_TAB.
  static constexpr freeink::ui::ActionId ACTION_SEARCH = ACTION_TAB_USER;
  static constexpr freeink::ui::ActionId ACTION_OPTIONS = ACTION_TAB_USER + 1;
  static constexpr freeink::ui::ActionId ACTION_REBUILD = ACTION_TAB_USER + 2;
  static void rebuildActionTrampoline(const freeink::ui::ActionEvent&, void* user);
  static void optionsActionTrampoline(const freeink::ui::ActionEvent&, void* user);
  static constexpr freeink::ui::ActionId ACTION_BACK = ACTION_REBUILD + 1;

  // Reconcile the card under the render lock with a static progress popup.
  bool rebuildIndex();
  void refreshLibrary();
  struct RefreshSelection {
    std::string paths[2];
    uint64_t hashes[2]{};
    uint32_t sizes[2]{};
    int entry = 0;
    int group = -1;
    bool collapsed = false;
    bool inGroup = false;
    bool tabFocus = true;
  };
  RefreshSelection captureRefreshSelection();
  void restoreRefreshSelection(const RefreshSelection& selection);
  void openOptions();
  void openGrouping();
  void openShelfFilter();
  void openBookOptions(int entry);
  void openBookDetails(const std::string& path, const std::string& title);
  void promptDeleteBook(const std::string& path, const std::string& title);
  bool resolveBook(int entry, std::string& path, std::string& title);
  bool hasFilter() const { return !query.empty() || shelfFilter != library::ShelfFilter::All; }
  bool seriesFor(int entry, std::string& name, uint16_t* position = nullptr, uint16_t* seriesId = nullptr);
  bool releaseIndexForChild();
  bool restoreIndexAfterChild(bool reopen);
  library::ShelfFilter shelfFilter = library::ShelfFilter::All;

  // Input
  void openSelectedBook();
  void openSearch();
  void promptRemoveRecentBook(const std::string& path, const std::string& title);
  bool collapseGroups(int bookEntry);
  void expandGroup(int groupEntry);
  void restoreExpandedList();
  void selectTab(int index, bool toggleIfActive);
  void toggleSortDirection();
  // Sub-screens act on button press, so a button still held when we resume must
  // not also act here. Records what to swallow on the next release.
  void swallowHeldReleases();
  // Staged back-out shared by Button::Back and the header back arrow.
  void handleBackAction();
  static void searchActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void backActionTrampoline(const freeink::ui::ActionEvent& event, void* user);

  // Data
  void applyFilter();
  void refilterAfterBookChange(int entry);
  void filterBooks();
  int totalBookRowCount() const;
  int bookRowCount() const;
  int rowFor(int entry) const;
  bool authorFor(int entry, std::string& author, std::string* authorSort = nullptr);
  bool rowTextFor(int entry, std::string& title, std::string& author, uint32_t* titleInitial = nullptr,
                  std::string* authorSort = nullptr);
  uint32_t titleInitialFor(int entry);
  bool buildGroupStarts();
  int groupForBook(int bookEntry) const;
  bool groupable() const;
  bool browsesGroups() const;

  // Screen building
  void buildHeader(UiScreen& screen);
  // Materializes ListItems and their strings for the visible window only.
  void buildRows(UiScreen& screen);
  static void formatInitialHeading(uint32_t initial, std::string& out);
  // Author group heading: the group's author sort, else a guess from the name.
  void authorHeadingFor(const std::string& author, const std::string& authorSort, std::string& out) const;
  void formatAuthorHeading(const std::string& author, std::string& out) const;
  void drawPositionReadout() const;
  void drawHoldHelp() const;
  const char* headerTitle() const override;

  // Ring 0 is the strip; the selected BOOK is ring - 1, with the strip keeping
  // row 0 as the working selection exactly as the pre-ring code did.
  int selectedEntry() const;
  bool tabsFocused() const { return ringPos() == 0; }
  bool showingRecents() const;

  library::LibraryIndexFile index;
  int activeTabIndex = 0;
  library::SortOrder sortOrder = library::SortOrder::AddedDesc;
  // One bit per tab; only Added starts descending (Recent has no direction).
  uint8_t descendingTabs = 1u << 1;
  // Set when the walk finished but the sort did not, so the screen can say the
  // order is discovery order rather than silently showing a wrong one.
  bool degraded = false;
  bool refreshFailed = false;

  // Rows surviving the current query, as positions in the active sort order.
  // Empty query means no filtering and this owns no allocation, so the ordinary
  // shelf pays nothing proportional to the library for the feature.
  std::string query;
  // Prepared when the filter changes, so rendering borrows stable text.
  std::string headerSearchTitle;
  std::unique_ptr<uint16_t[]> filtered;
  uint16_t filteredCount = 0;
  bool filterFailed = false;

  // One start row per group: at most 8 KiB at the 4096-book index limit.
  // This fallible allocation is reused after its first successful allocation.
  std::unique_ptr<uint16_t[]> groupStarts;
  uint16_t groupCapacity = 0;
  uint16_t groupCount = 0;
  bool groupsCollapsed = false;
  int selectedGroup = -1;
  // Only the selected group's caption is retained, not every group name.
  std::string groupTitle;
  freeink::ui::ListNav expandedNav;

  // Visible-window row storage, reused across renders (buildRows). Bounded by
  // the densest page, never by the library. Headings get their own storage:
  // the surname-first inversion must not overwrite the author slot, whose raw
  // value the next row's group comparison reads.
  std::vector<freeink::ui::ListItem> winItems;
  std::vector<std::string> winTitles;
  std::vector<std::string> winAuthors;
  std::vector<std::string> winHeaders;

  bool lockNextConfirmRelease = false;
  bool lockNextBackRelease = false;
};
