#include "LibraryListActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibrarySession.h>
#include <LibraryText.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "LibraryBookDetailsActivity.h"
#include "LibraryMenuActivity.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/icons/headerIcons.h"
#include "components/icons/listIcons.h"
#include "components/icons/search32.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/LibraryRelink.h"

namespace fui = freeink::ui;

namespace {
constexpr int SIDE_PADDING = 12;
constexpr unsigned long LONG_PRESS_MS = 1000;

constexpr int RECENT_TAB = 0;
constexpr int ADDED_TAB = 1;
constexpr int TITLE_TAB = 2;
constexpr int AUTHOR_TAB = 3;
constexpr int TAB_SLOTS = AUTHOR_TAB + 1;

constexpr bool isDescending(const library::SortOrder order) {
  return order == library::SortOrder::AddedDesc || order == library::SortOrder::TitleDesc ||
         order == library::SortOrder::AuthorDesc || order == library::SortOrder::SeriesDesc;
}

constexpr bool isAddedSort(const library::SortOrder order) {
  return order == library::SortOrder::AddedAsc || order == library::SortOrder::AddedDesc;
}

constexpr bool isAuthorSort(const library::SortOrder order) {
  return order == library::SortOrder::AuthorAsc || order == library::SortOrder::AuthorDesc;
}

constexpr bool isSeriesSort(const library::SortOrder order) {
  return order == library::SortOrder::SeriesAsc || order == library::SortOrder::SeriesDesc;
}

library::SortOrder orderForTab(const int tab, const uint8_t descendingTabs) {
  const bool descending = (descendingTabs & (1u << tab)) != 0;
  if (tab == TITLE_TAB) return descending ? library::SortOrder::TitleDesc : library::SortOrder::TitleAsc;
  if (tab == AUTHOR_TAB) {
    if (SETTINGS.libraryGroupBySeries)
      return descending ? library::SortOrder::SeriesDesc : library::SortOrder::SeriesAsc;
    return descending ? library::SortOrder::AuthorDesc : library::SortOrder::AuthorAsc;
  }
  return descending ? library::SortOrder::AddedDesc : library::SortOrder::AddedAsc;
}

const char* tabLabelFor(const int tab) {
  if (tab == RECENT_TAB) return tr(STR_LIBRARY_TAB_RECENT);
  if (tab == TITLE_TAB) return tr(STR_LIBRARY_TAB_TITLE);
  if (tab == AUTHOR_TAB) return SETTINGS.libraryGroupBySeries ? tr(STR_LIBRARY_SERIES) : tr(STR_LIBRARY_TAB_AUTHOR);
  return tr(STR_LIBRARY_TAB_TIME);
}

}  // namespace

LibraryListActivity::LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiTabListActivity("Library", renderer, mappedInput, true) {}

void LibraryListActivity::onEnter() {
  // One lock across the base lifecycle AND the data phase: the base onEnter
  // schedules a paint, and the render task must not read the index or the
  // filter before they are in place. The rebuild also needs the lock: the
  // render task's SD-loaded fonts read glyph data at draw time, and the walk
  // needs the card to itself.
  RenderLock lock(*this);
  UiTabListActivity::onEnter();
  app.on(ACTION_SEARCH, &LibraryListActivity::searchActionTrampoline, this);
  app.on(ACTION_OPTIONS, &LibraryListActivity::optionsActionTrampoline, this);
  app.on(ACTION_REBUILD, &LibraryListActivity::rebuildActionTrampoline, this);
  app.on(ACTION_BACK, &LibraryListActivity::backActionTrampoline, this);

  const bool validIndex = index.open(library::libraryIndexPath());
  const bool matchingMetadata = validIndex && index.header().metadataEnabled == (SETTINGS.libraryUseMetadata != 0);
  if (library::isLibraryIndexDirty() || library::librarySession.needsRefresh(validIndex, matchingMetadata)) {
    index.close();
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    const auto refreshToken = library::librarySession.refreshToken();
    refreshFailed = !rebuildIndex();
    library::librarySession.reconciled(!refreshFailed, refreshToken);
    if (!index.open(library::libraryIndexPath())) {
      LOG_ERR("LIB", "cannot open library index");
      refreshFailed = true;
      library::librarySession.invalidate();
    }
  }
  // Recent is backed by the resident store. A refresh above relinks renamed
  // books, so it runs first and only the entries still missing are pruned. The
  // index is released around the store's persistence write.
  if (RECENT_BOOKS.pruneMissing()) {
    index.close();
    RECENT_BOOKS.saveToFile();
    if (!index.open(library::libraryIndexPath())) {
      LOG_ERR("LIB", "cannot reopen library index");
      refreshFailed = true;
      library::librarySession.invalidate();
    }
  }
  degraded = index.isOpen() && index.ranksDegraded();
  if (index.isOpen() && index.dedupDegraded()) {
    LOG_ERR("LIB", "index was built without duplicate detection");
  }

  // Entered while Confirm was still held (typical when launched from the home
  // menu): ignore its release, or we would open whatever sits at row 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate(true);
}

void LibraryListActivity::loop() {
  // Index seeks and filter/group buffers are shared with the render task.
  // Own the lock across dispatch; helpers and touch callbacks must not relock.
  RenderLock lock(*this);
  if (handleCustomInput() || handleButtons() || routeListTouch()) return;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    auto& nav = activeNav();
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? nav.inputPageRows() : -nav.inputPageRows();
    nav.requestScroll(delta);
    requestUpdate();
    return;
  }
  navigateButtons();
}

void LibraryListActivity::onExit() {
  index.close();
  Activity::onExit();
}

bool LibraryListActivity::rebuildIndex() {
  library::BuildStats stats;
  const bool ok = library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0, relinkRenamedBook);
  if (!ok) {
    LOG_ERR("LIB", "index build failed");
    return false;
  }
  LOG_INF("LIB",
          "reconciled: %u unchanged, %u added, %u renamed (%u kept reading state), %u removed, %u enriched (%u dup, %u "
          "unreadable)",
          static_cast<unsigned>(stats.unchanged), static_cast<unsigned>(stats.added),
          static_cast<unsigned>(stats.renamed), static_cast<unsigned>(stats.relinked),
          static_cast<unsigned>(stats.removed), static_cast<unsigned>(stats.enriched),
          static_cast<unsigned>(stats.duplicatesDropped), static_cast<unsigned>(stats.unreadableSkipped));
  if (stats.dedupDegraded) LOG_ERR("LIB", "rebuild completed without duplicate detection");
  return true;
}

void LibraryListActivity::swallowHeldReleases() {
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  lockNextBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
}

int LibraryListActivity::selectedEntry() const {
  const int entry = ringPos() - 1;
  return entry < 0 ? 0 : entry;
}

bool LibraryListActivity::showingRecents() const { return activeTabIndex == RECENT_TAB; }

void LibraryListActivity::openSelectedBook() {
  if (selectedEntry() >= bookRowCount()) return;
  std::string path;
  if (showingRecents()) {
    const auto& books = RECENT_BOOKS.getBooks();
    if (selectedEntry() >= static_cast<int>(books.size())) return;
    path = books[static_cast<size_t>(selectedEntry())].path;
  } else {
    if (!index.isOpen()) return;
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(selectedEntry())));
    if (ordinal == 0xFFFF) return;

    library::ClixRecord record{};
    if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) {
      LOG_ERR("LIB", "cannot resolve path for row %d", selectedEntry());
      return;
    }
  }
  // The reader screen this opens has its own surfaces; a lingering tap flash
  // would gray an unrelated element there.
  app.clearTapFlash();
  // Release the index handle first: on hardware only one reader can hold a file
  // open at a time, and the reader is about to open files of its own.
  index.close();
  onSelectBook(path);
}

void LibraryListActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  if (groupsCollapsed) {
    expandGroup(index);
  } else {
    openSelectedBook();
  }
}

void LibraryListActivity::onRowLongPress(const int entry) {
  if (groupsCollapsed)
    expandGroup(entry);
  else
    openBookOptions(entry);
}

void LibraryListActivity::promptRemoveRecentBook(const std::string& path, const std::string& title) {
  const bool reopenIndex = index.isOpen();
  index.close();
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: recent removal confirmation");
    restoreIndexAfterChild(reopenIndex);
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path, reopenIndex](const ActivityResult& result) {
    RenderLock lock(*this);
    swallowHeldReleases();
    if (!result.isCancelled && RECENT_BOOKS.removeByPath(path)) {
      closeRouting();
      auto& nav = activeNav();
      const int count = listCount();
      if (count == 0) {
        nav.selected = 0;
      } else if (nav.selected > count) {
        nav.selected = count;
      }
      nav.followOnBuild = true;
    }
    restoreIndexAfterChild(reopenIndex);
  });
}

void LibraryListActivity::openSearch() {
  app.clearTapFlash();
  // No key filtering here on purpose. Greying out the letters that lead nowhere
  // was built, tested on device and removed: a letter you can see but cannot
  // reach reads as a broken keyboard, and the eye keeps returning to it.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_LIBRARY_SEARCH), query, 48,
                                                           InputType::Text);
  if (!keyboard) {
    LOG_ERR("LIB", "OOM: search keyboard");
    return;
  }
  const bool reopenIndex = releaseIndexForChild();
  startActivityForResult(std::move(keyboard), [this, reopenIndex](const ActivityResult& result) {
    RenderLock lock(*this);
    swallowHeldReleases();
    if (!restoreIndexAfterChild(reopenIndex)) return;
    if (result.isCancelled) return;
    if (showingRecents()) selectTab(ADDED_TAB, false);
    query = std::get<KeyboardResult>(result.data).text;
    applyFilter();
    auto& nav = activeNav();
    if (!query.empty() && filteredCount == 0 && !degraded) {
      // Up from the tab bar reopens Search even with no results.
      nav.reset();
    } else {
      // A non-empty result belongs to the list: land on
      // its first surviving row, not on the strip.
      nav.reset(1);
    }
    requestUpdate();
  });
}

void LibraryListActivity::stepTab(const int direction) {
  const int next = (activeTab() + (direction > 0 ? 1 : TAB_SLOTS - 1)) % TAB_SLOTS;
  selectTab(next, false);
}

void LibraryListActivity::onTabAction(const int index) {
  app.clearTapFlash();
  selectTab(index, true);
}

void LibraryListActivity::selectTab(const int index, const bool toggleIfActive) {
  if (index < 0 || index >= TAB_SLOTS) return;
  if (index != RECENT_TAB && toggleIfActive && index == activeTab())
    descendingTabs ^= static_cast<uint8_t>(1u << index);
  closeRouting();
  activeTabIndex = index;
  if (index != RECENT_TAB) {
    sortOrder = orderForTab(index, descendingTabs);
    // The filter holds positions in the old order, so it must be rebuilt.
    applyFilter();
  } else {
    shelfFilter = library::ShelfFilter::All;
    query.clear();
    applyFilter();
  }
  // Tab changes happen only while the bar owns focus. A tab's remembered row
  // must not pull focus back into the list after the switch.
  auto& nav = activeNav();
  nav.reset();
  requestUpdate();
}

void LibraryListActivity::toggleSortDirection() { selectTab(activeTab(), true); }

int LibraryListActivity::tabCount() const { return TAB_SLOTS; }

int LibraryListActivity::activeTab() const { return activeTabIndex; }

const char* LibraryListActivity::tabLabel(const int index) const { return tabLabelFor(index); }

fui::TabIndicator LibraryListActivity::tabIndicator(const int index) const {
  if (index != activeTab() || index == RECENT_TAB) return fui::TabIndicator::None;
  return isDescending(sortOrder) ? fui::TabIndicator::Down : fui::TabIndicator::Up;
}

int LibraryListActivity::totalBookRowCount() const {
  if (showingRecents()) return static_cast<int>(RECENT_BOOKS.getBooks().size());
  return hasFilter() ? static_cast<int>(filteredCount) : static_cast<int>(index.bookCount());
}

int LibraryListActivity::bookRowCount() const {
  if (selectedGroup < 0) return totalBookRowCount();
  if (!index.isOpen() || selectedGroup >= groupCount) return 0;
  const int end = selectedGroup + 1 < groupCount ? groupStarts[selectedGroup + 1] : totalBookRowCount();
  return end - groupStarts[selectedGroup];
}

int LibraryListActivity::listCount() const { return groupsCollapsed ? static_cast<int>(groupCount) : bookRowCount(); }

// Entry position on screen to row position in the sort order. Identity while
// unfiltered, so the shelf costs nothing when nothing is typed.
int LibraryListActivity::rowFor(const int entry) const {
  if (entry < 0 || entry >= bookRowCount()) return -1;
  const int bookEntry = entry + (selectedGroup < 0 ? 0 : groupStarts[selectedGroup]);
  if (!hasFilter()) return bookEntry;
  if (!filtered) return -1;
  return filtered[bookEntry];
}

bool LibraryListActivity::browsesGroups() const {
  return !showingRecents() && (isAuthorSort(sortOrder) || isSeriesSort(sortOrder));
}

bool LibraryListActivity::authorFor(const int entry, std::string& author, std::string* authorSort) {
  author.clear();
  if (authorSort) authorSort->clear();
  if (entry < 0 || entry >= bookRowCount()) return false;
  const uint16_t ordinal = index.ordinalForRow(sortOrder, rowFor(entry));
  library::ClixRecord record{};
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) return false;
  // Missing author metadata is a valid Unknown Author group.
  if (!index.readAuthor(record, author)) return false;
  if (authorSort && !index.readAuthorSort(record, *authorSort)) return false;
  return !index.ioFailed();
}

bool LibraryListActivity::groupable() const {
  return !showingRecents() && !degraded && !isAddedSort(sortOrder) && bookRowCount() > 0;
}

uint32_t LibraryListActivity::titleInitialFor(const int entry) {
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) return 0;
  return library::foldedGroupInitial(std::string_view(record.fold, record.foldLen));
}

bool LibraryListActivity::buildGroupStarts() {
  if (selectedGroup >= 0) return false;
  const int count = bookRowCount();
  if (count <= 0) return false;
  if (groupCapacity < count) {
    auto starts = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(count));
    if (!starts) {
      LOG_ERR("LIB", "cannot allocate %u-byte group map", static_cast<unsigned>(count * sizeof(uint16_t)));
      return false;
    }
    groupStarts = std::move(starts);
    groupCapacity = static_cast<uint16_t>(count);
  }

  groupCount = 0;
  uint32_t previousInitial = 0;
  char previousAuthorKey[library::CLIX_AUTHOR_KEY_BYTES] = {};
  uint8_t previousAuthorKeyLen = 0;
  std::string previousAuthor;
  uint16_t previousSeries = library::CLIX_SERIES_NONE;
  std::string author;
  author.reserve(128);
  for (int entry = 0; entry < count; entry++) {
    if ((entry & 31) == 0) delay(1);
    bool startsGroup = entry == 0;
    if (isSeriesSort(sortOrder)) {
      const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
      library::ClixSeriesRef ref{};
      if (ordinal == 0xFFFF || !index.readSeriesRef(ordinal, ref)) {
        groupCount = 0;
        return false;
      }
      startsGroup = startsGroup || ref.seriesId != previousSeries;
      // The series table is immutable while open; validate its shared entry once per group.
      if (startsGroup && ref.seriesId != library::CLIX_SERIES_NONE) {
        uint16_t seriesCount = 0;
        if (!index.readSeries(ref.seriesId, author, seriesCount)) {
          groupCount = 0;
          return false;
        }
      }
      previousSeries = ref.seriesId;
    } else if (isAuthorSort(sortOrder)) {
      const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
      library::ClixRecord record{};
      if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) {
        groupCount = 0;
        return false;
      }
      startsGroup = startsGroup || record.authorKeyLen != previousAuthorKeyLen ||
                    memcmp(record.authorKey, previousAuthorKey, record.authorKeyLen) != 0;
      if (record.authorKeyLen == 0) {
        // Initials-only names have no normalized identity; retain their labels.
        if (!index.readAuthor(record, author)) {
          groupCount = 0;
          return false;
        }
        startsGroup = startsGroup || author != previousAuthor;
        previousAuthor = author;
      }
      memcpy(previousAuthorKey, record.authorKey, record.authorKeyLen);
      previousAuthorKeyLen = record.authorKeyLen;
    } else {
      const uint32_t initial = titleInitialFor(entry);
      startsGroup = startsGroup || initial != previousInitial;
      previousInitial = initial;
    }
    if (index.ioFailed()) {
      groupCount = 0;
      return false;
    }
    if (startsGroup) groupStarts[groupCount++] = static_cast<uint16_t>(entry);
  }
  LOG_DBG("LIB", "group map: %u groups, %u bytes", static_cast<unsigned>(groupCount),
          static_cast<unsigned>(groupCapacity * sizeof(uint16_t)));
  return groupCount > 0;
}

int LibraryListActivity::groupForBook(const int bookEntry) const {
  int group = 0;
  while (group + 1 < groupCount && groupStarts[group + 1] <= bookEntry) group++;
  return group;
}

bool LibraryListActivity::collapseGroups(const int bookEntry) {
  if (selectedGroup >= 0) {
    closeRouting();
    selectedGroup = -1;
    groupTitle.clear();
    groupsCollapsed = true;
    activeNav() = expandedNav;
    requestUpdate();
    return true;
  }
  if (groupsCollapsed) return true;
  if (!groupable() || !buildGroupStarts()) return false;
  closeRouting();
  expandedNav = activeNav();
  groupsCollapsed = true;
  auto& nav = activeNav();
  nav.reset(groupForBook(bookEntry) + 1);
  requestUpdate();
  return true;
}

void LibraryListActivity::expandGroup(const int groupEntry) {
  if (!groupsCollapsed || groupEntry < 0 || groupEntry >= groupCount) return;
  if (browsesGroups()) {
    const int bookEntry = groupStarts[groupEntry];
    if (isSeriesSort(sortOrder)) {
      if (!seriesFor(bookEntry, groupTitle)) return;
    } else {
      std::string author;
      std::string authorSort;
      if (!authorFor(bookEntry, author, &authorSort)) return;
      authorHeadingFor(author, authorSort, groupTitle);
    }
    closeRouting();
    expandedNav = activeNav();
    // Touch and button activation both return to the group that was opened.
    expandedNav.selected = groupEntry + 1;
    selectedGroup = groupEntry;
    groupsCollapsed = false;
    activeNav().reset(1);
    requestUpdate();
    return;
  }
  closeRouting();
  const int bookEntry = groupStarts[groupEntry];
  groupsCollapsed = false;
  activeNav() = expandedNav;
  auto& nav = activeNav();
  nav.selected = bookEntry + 1;
  nav.top = bookEntry;
  nav.followOnBuild = true;
  requestUpdate();
}

void LibraryListActivity::restoreExpandedList() {
  if (!groupsCollapsed || browsesGroups()) return;
  closeRouting();
  groupsCollapsed = false;
  activeNav() = expandedNav;
  requestUpdate();
}

void LibraryListActivity::applyFilter() {
  selectedGroup = -1;
  groupTitle.clear();
  filterBooks();
  if (!browsesGroups()) return;
  groupsCollapsed = true;
  if (filterFailed || totalBookRowCount() == 0) return;
  if (degraded || !buildGroupStarts()) {
    groupCount = 0;
    filterFailed = true;
    LOG_ERR("LIB", "library groups unavailable");
  }
}

void LibraryListActivity::refilterAfterBookChange(const int entry) {
  // Only the state predicate can change; the query and index order are stable.
  if (shelfFilter == library::ShelfFilter::All) return;
  const int row = rowFor(entry);
  const int previousGroup = selectedGroup;
  const int first = previousGroup < 0 ? 0 : rowFor(0);
  const int end = previousGroup < 0 ? index.bookCount() : rowFor(bookRowCount() - 1) + 1;
  const auto previousNav = activeNav();
  std::string previousTitle = std::move(groupTitle);
  applyFilter();
  if (filterFailed || row < 0 || first < 0 || end <= first) {
    activeNav().reset();
    return;
  }

  // Filtering preserves sort-row order. Prefer the same book, then the next
  // survivor in its group, or the previous survivor when it was the last row.
  int match = -1;
  for (int candidate = 0; candidate < totalBookRowCount(); ++candidate) {
    const int candidateRow = rowFor(candidate);
    if (candidateRow < first) continue;
    if (candidateRow >= end) break;
    match = candidate;
    if (candidateRow >= row) break;
  }
  if (previousGroup >= 0) {
    if (match < 0) {
      activeNav() = expandedNav;
      activeNav().requestSelection(groupCount == 0 ? 0 : std::min(previousGroup, static_cast<int>(groupCount) - 1) + 1);
      return;
    }
    selectedGroup = groupForBook(match);
    groupsCollapsed = false;
    groupTitle = std::move(previousTitle);
    expandedNav.requestSelection(selectedGroup + 1);
    match -= groupStarts[selectedGroup];
  }
  activeNav() = previousNav;
  activeNav().requestSelection(match + 1);
}

// One pass over the sort order. The stored prefix handles short titles; a
// truncated prefix falls back to the full title/name blob. The result array is
// allocated once with the exact upper bound and fails back to an explicit
// message rather than letting vector growth abort the firmware.
void LibraryListActivity::filterBooks() {
  closeRouting();
  activeNav().requestSelection(activeNav().selected.load());
  groupsCollapsed = false;
  groupCount = 0;
  filtered.reset();
  filteredCount = 0;
  filterFailed = false;
  headerSearchTitle = query.empty() ? std::string{} : "\"" + query + "\"";
  if (!hasFilter()) return;
  const int total = index.bookCount();
  if (total == 0) return;
  auto matches = makeUniqueNoThrow<uint16_t[]>(total);
  if (!matches) {
    LOG_ERR("LIB", "OOM: library filter map");
    filterFailed = true;
    return;
  }
  const std::string needle = library::fold(query);
  // Title/name/author blob lengths are u8. Reuse one string for every field,
  // allocating only when a stored prefix misses and a blob must be read.
  std::string candidateText;
  std::string candidateTitle;
  std::string foldedCandidate;
  uint16_t lastSeriesId = library::CLIX_SERIES_NONE;
  bool lastSeriesMatches = false;
  for (int row = 0; row < total; ++row) {
    if ((row & 31) == 0) delay(1);
    const uint16_t ordinal = index.ordinalForRow(sortOrder, row);
    library::ClixRecord record{};
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) {
      filterFailed = true;
      break;
    }
    bool textMatches = needle.empty() || library::matchesQuery(std::string_view(record.fold, record.foldLen), needle);
    if (!textMatches && candidateText.capacity() < UINT8_MAX) candidateText.reserve(UINT8_MAX);
    if (!textMatches && candidateTitle.capacity() < UINT8_MAX) candidateTitle.reserve(UINT8_MAX);
    // The stored fold is the title SORT, which a Calibre library may curate away
    // from the shown title ("Dune 02" for "Dune Messiah"), and is capped at 96
    // bytes. The shown title comes from the same blob pass as the author.
    if (!textMatches) {
      if (!index.readTitleAndAuthor(record, candidateTitle, candidateText)) {
        filterFailed = true;
        break;
      }
      // A four-byte UTF-8 codepoint can leave the 96-byte prefix three bytes short.
      if (candidateTitle.empty() && record.foldLen >= library::CLIX_FOLD_BYTES - 3) {
        if (!index.readName(record, candidateTitle)) {
          filterFailed = true;
          break;
        }
        // Match the builder's filename-stem fallback, excluding the extension.
        const size_t dot = candidateTitle.find_last_of('.');
        if (dot != std::string::npos && dot != 0) candidateTitle.resize(dot);
      }
      if (!candidateTitle.empty()) {
        library::foldInto(candidateTitle, foldedCandidate);
        textMatches = library::matchesQuery(foldedCandidate, needle);
      }
    }
    if (!textMatches) {
      library::foldInto(candidateText, foldedCandidate);
      textMatches = library::matchesQuery(foldedCandidate, needle);
    }
    if (!textMatches) {
      library::ClixSeriesRef ref{};
      if (!index.readSeriesRef(ordinal, ref)) {
        filterFailed = true;
        break;
      }
      if (ref.seriesId != library::CLIX_SERIES_NONE) {
        // The series table and query are immutable during this filter pass.
        if (ref.seriesId != lastSeriesId) {
          uint16_t count = 0;
          if (!index.readSeries(ref.seriesId, candidateText, count)) {
            filterFailed = true;
            break;
          }
          library::foldInto(candidateText, foldedCandidate);
          lastSeriesMatches = library::matchesQuery(foldedCandidate, needle);
          lastSeriesId = ref.seriesId;
        }
        textMatches = lastSeriesMatches;
      }
    }
    if (index.ioFailed()) {
      filterFailed = true;
      break;
    }
    if (!textMatches) continue;
    // State lives in individual files; only consult it for text candidates.
    if (shelfFilter != library::ShelfFilter::All) {
      uint64_t key = 0;
      library::BookState state;
      if (!index.readPathHash(record, key) || !library::readBookState(key, state)) {
        filterFailed = true;
        break;
      }
      if (!library::matchesShelfFilter(state, shelfFilter)) continue;
    }
    matches[filteredCount++] = row;
  }
  if (filterFailed) {
    filteredCount = 0;
    LOG_ERR("LIB", "library filter read failed");
    return;
  }
  filtered = std::move(matches);
}

// Staged back-out, shared by the Back button and the header's back arrow:
// leave the author/series, clear the search, return focus to the tabs, then home.
void LibraryListActivity::handleBackAction() {
  auto& nav = activeNav();
  if (selectedGroup >= 0) {
    collapseGroups(selectedEntry());
  } else if (!showingRecents() && !query.empty()) {
    query.clear();
    applyFilter();
    nav.selected = 0;
    nav.top = 0;
    requestUpdate();
  } else if (groupsCollapsed && !browsesGroups()) {
    restoreExpandedList();
  } else if (!tabsFocused() && !degraded) {
    // Keep the current list and viewport while returning focus to the tabs.
    nav.selected = 0;
    requestUpdate();
  } else {
    onGoHome();
  }
}

void LibraryListActivity::searchActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->openSearch();
}

void LibraryListActivity::backActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->handleBackAction();
}

// Title and author for one entry, read straight from the index. Only ever
// called for rows about to be drawn, so at most a screenful of strings exists
// at once.
bool LibraryListActivity::rowTextFor(const int entry, std::string& title, std::string& author, uint32_t* titleInitial,
                                     std::string* authorSort) {
  title.clear();
  author.clear();
  if (authorSort) authorSort->clear();
  if (titleInitial) *titleInitial = 0;
  if (entry < 0 || entry >= bookRowCount()) return false;
  if (showingRecents()) {
    const auto& books = RECENT_BOOKS.getBooks();
    if (entry >= static_cast<int>(books.size())) return false;
    const auto& book = books[static_cast<size_t>(entry)];
    title = book.title;
    author = book.author;
    return true;
  }
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) return false;
  if (titleInitial) *titleInitial = library::foldedGroupInitial(std::string_view(record.fold, record.foldLen));
  // The index stores one spelling per author; absent metadata uses the filename.
  const bool read = authorSort ? index.readTitleAuthorAndSort(record, title, author, *authorSort)
                               : index.readTitleAndAuthor(record, title, author);
  if (!read) return false;
  if (title.empty() && !index.readName(record, title)) return false;
  if (index.ioFailed()) return false;
  if (title.empty()) title = tr(STR_LIBRARY_UNKNOWN_TITLE);
  return true;
}

bool LibraryListActivity::handleCustomInput() {
  if (lockNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lockNextConfirmRelease = false;
    return true;
  }
  if (lockNextBackRelease && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockNextBackRelease = false;
    return true;
  }

  return false;
}

bool LibraryListActivity::handleButtons() {
  const int count = listCount();

  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS)) {
    if (tabsFocused())
      openOptions();
    else if (count > 0)
      onRowLongPress(selectedEntry());
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    handleBackAction();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (tabsFocused()) {
      stepTab(1);
      return true;
    }
    if (count > 0) activateIndex(selectedEntry());
    return true;
  }

  return false;
}

void LibraryListActivity::navigateButtons() {
  const int count = listCount();
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    navigationStartedOnTabs = tabsFocused();
  }
  buttonNavigator.onNextPress([this, count] {
    if (count > 0) moveRingTo(ringPos() == count ? 1 : ringPos() + 1);
  });
  buttonNavigator.onPreviousPress([this, count] {
    if ((!navigationStartedOnTabs || degraded) && count > 0) {
      moveRingTo(ringPos() <= 1 ? count : ringPos() - 1);
    }
  });
  // Search is an activation: defer it so holding Previous can still step tabs.
  buttonNavigator.onPreviousRelease([this] {
    if (navigationStartedOnTabs && tabsFocused() && !degraded) openSearch();
  });
  // A held button steps tabs while the strip has focus (the base behaviour
  // Settings keeps) and page-jumps once the selection is down in the rows,
  // where fast travel through a long shelf is what a hold means.
  buttonNavigator.onNextContinuous([this, count] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
      stepTab(1);
    } else if (count > 0) {
      moveRingTo(ButtonNavigator::nextPageIndex(selectedEntry(), count, activeNav().inputPageRows()) + 1);
    }
  });
  buttonNavigator.onPreviousContinuous([this, count] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
      stepTab(-1);
    } else if (count > 0) {
      moveRingTo(ButtonNavigator::previousPageIndex(selectedEntry(), count, activeNav().inputPageRows()) + 1);
    }
  });
}

void LibraryListActivity::buildRows(UiScreen& screen) {
  auto& nav = activeNav();
  const int count = listCount();
  const bool seriesGrouped = !showingRecents() && isSeriesSort(sortOrder);
  const bool authorGrouped = !showingRecents() && isAuthorSort(sortOrder);
  const bool grouped = !showingRecents() && !isAddedSort(sortOrder);

  fui::ListProps props;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;
  props.rowGap = std::max<int16_t>(screen.theme().listRowGap, 6);
  props.headerUnderline = false;
  syncTabListViewport(screen, props);

  // The content-sized SDK layout may draw a clipped trailing row.
  const size_t cap = static_cast<size_t>(nav.visibleRows > 0 ? nav.visibleRows : 1) + 1;
  if (winTitles.size() < cap) winTitles.resize(cap);
  if (winAuthors.size() < cap) winAuthors.resize(cap);
  if (!groupsCollapsed && winHeaders.size() < cap) winHeaders.resize(cap);
  winItems.clear();
  if (winItems.capacity() < cap) winItems.reserve(cap);

  int rows = 0;
  int headers = 0;
  uint32_t previousInitial = 0;
  uint16_t previousSeries = library::CLIX_SERIES_NONE;
  // Capture this after syncTabListViewport(), which may clamp nav.top.
  const int windowStart = static_cast<int>(props.topIndex);
  std::string rowAuthorSort;  // the author sort behind an author heading
  for (int entry = windowStart; entry < count && rows < static_cast<int>(cap); entry++) {
    std::string& title = winTitles[static_cast<size_t>(rows)];
    std::string& author = winAuthors[static_cast<size_t>(rows)];
    fui::ListItem item;
    if (groupsCollapsed) {
      const int bookEntry = groupStarts[entry];
      if (seriesGrouped) {
        if (!seriesFor(bookEntry, title)) break;
      } else if (authorGrouped) {
        if (!authorFor(bookEntry, author, &rowAuthorSort)) break;
        authorHeadingFor(author, rowAuthorSort, title);
      } else {
        formatInitialHeading(titleInitialFor(bookEntry), title);
      }
      const int end = entry + 1 < groupCount ? groupStarts[entry + 1] : totalBookRowCount();
      char countText[8];
      snprintf(countText, sizeof(countText), "%d", end - bookEntry);
      // Group rows have no author subtitle; reuse its visible-window storage.
      author = countText;
      item.value = author.c_str();
    } else {
      uint32_t initial = 0;
      if (!rowTextFor(entry, title, author, &initial, authorGrouped ? &rowAuthorSort : nullptr)) break;
      bool startsGroup = false;
      if (seriesGrouped) {
        std::string& heading = winHeaders[static_cast<size_t>(headers)];
        uint16_t position = library::SERIES_INDEX_NONE;
        uint16_t seriesId = library::CLIX_SERIES_NONE;
        if (!seriesFor(entry, heading, &position, &seriesId)) break;
        if (selectedGroup < 0 && (rows == 0 || seriesId != previousSeries)) {
          item.sectionHeading = heading.c_str();
          ++headers;
        }
        previousSeries = seriesId;
        if (position != library::SERIES_INDEX_NONE) {
          char number[16];
          library::formatSeriesIndex(position, number, sizeof(number));
          title.insert(0, " · ");
          title.insert(0, number);
        }
      } else if (authorGrouped) {
        startsGroup = selectedGroup < 0 && (rows == 0 || author != winAuthors[static_cast<size_t>(rows - 1)]);
      } else if (grouped) {
        startsGroup = rows == 0 || initial != previousInitial;
        previousInitial = initial;
      }
      if (startsGroup) {
        std::string& heading = winHeaders[static_cast<size_t>(headers++)];
        if (authorGrouped)
          authorHeadingFor(author, rowAuthorSort, heading);
        else
          formatInitialHeading(initial, heading);
        item.sectionHeading = heading.c_str();
      }
      if (!authorGrouped && !author.empty()) item.subtitle = author.c_str();
    }

    item.label = title.c_str();
    if (showingRecents()) item.icon = listIconFor(UITheme::getFileIcon(RECENT_BOOKS.getBooks()[entry].path), 32);
    item.actionValue = static_cast<int16_t>(entry);
    winItems.push_back(item);
    rows++;
  }

  // A zero-sized window means an unwindowed array to the SDK list widget.
  if (winItems.empty()) {
    screen.centeredText(tr(STR_LIBRARY_VIEW_UNAVAILABLE));
    return;
  }
  props.items = winItems.data();
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  props.itemsWindowCount = static_cast<uint16_t>(winItems.size());
  screen.list(props);
}

void LibraryListActivity::formatInitialHeading(uint32_t initial, std::string& out) {
  out.clear();
  if (initial == 0) {
    out.push_back('#');
    return;
  }
  if (initial >= 'a' && initial <= 'z') initial -= 'a' - 'A';
  utf8AppendCodepoint(initial, out);
}

void LibraryListActivity::authorHeadingFor(const std::string& author, const std::string& authorSort,
                                           std::string& out) const {
  // The author sort the books name ("Acemoglu, Daron & Robinson, James A.") is
  // what the shelf is ordered by; formatAuthorHeading() only guesses at it.
  // Publishers sometimes write it in capitals ("HUNA, KUGA"), which orders
  // correctly but reads as shouting, so such a sort is only used for order.
  const auto hasLowercase = [](const std::string& text) {
    return std::any_of(text.begin(), text.end(), [](const char c) { return c >= 'a' && c <= 'z'; });
  };
  if (!author.empty() && !authorSort.empty() && (hasLowercase(authorSort) || !hasLowercase(author))) {
    out = authorSort;
    return;
  }
  formatAuthorHeading(author, out);
}

void LibraryListActivity::formatAuthorHeading(const std::string& author, std::string& out) const {
  if (author.empty()) {
    out = tr(STR_LIBRARY_UNKNOWN_AUTHOR);
    return;
  }
  const size_t lastSpace = author.find_last_of(' ');
  if (lastSpace == std::string::npos || lastSpace + 1 == author.size()) {
    out = author;
    return;
  }
  const size_t surnameLength = author.size() - lastSpace - 1;
  out.reserve(author.size() + 1);
  out = author;
  // Rotate whole UTF-8 segments; the separating ASCII space ends up last.
  std::rotate(out.begin(), out.begin() + lastSpace + 1, out.end());
  out.pop_back();
  out.insert(surnameLength, ", ");
}

void LibraryListActivity::buildHeader(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  fui::HeaderProps header;
  header.title = headerTitle();
  header.leadingIcon = listIconFor(UIIcon::Blocks, 32);
  header.leadingAction = ACTION_OPTIONS;
  header.titleText = theme.titleText;
  header.titleText.align = theme.headerTitleAlign;
  header.sidePadding = theme.headerSidePadding;
  header.minTouchSize = theme.minTouchSize;
  header.styles = theme.popup;
  if (header.styles.normal.border.kind == fui::PaintKind::None && theme.headerUnderline > 0) {
    header.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    header.styles.normal.borderWidth = theme.headerUnderline;
  }
  header.trailingStyles = fui::plainStyles(fui::Paint::solid(fui::Color::Black));
  header.borderEdges = fui::EdgeBottom;
  // Same battery/clock band as every GUI.drawHeader screen; the header
  // heights are unified across themes, so the buttons derive from the band.
  GUI.applyHeaderStatus(renderer, header);
  if (mappedInput.hasTouch()) {
    header.leadingIcon = fui::bitmapFromIcon(icon_header_back_32);
    header.leadingAction = ACTION_BACK;
  }
  if (!degraded) {
    header.trailingIcon = fui::bitmapFromIcon(icon_search_32);
    header.trailingAction = ACTION_SEARCH;
  }
  if (mappedInput.hasTouch()) {
    header.trailingAdjacentIcon = listIconFor(UIIcon::Blocks, 32);
    header.trailingAdjacentAction = ACTION_OPTIONS;
  }
  const auto frameRect = screen.frame().screen();
  // Header and tabs share a screen-relative boundary, independent of bezel insets.
  fui::header(screen.frame(),
              fui::Rect{frameRect.x, static_cast<int16_t>(metrics.topPadding), frameRect.width,
                        static_cast<int16_t>(metrics.headerHeight)},
              header);
}

void LibraryListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The position readout owns the line above the hints; rows must not overlap
  // it.
  const int16_t readoutReserved = static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing);
  buildHeader(screen);
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight + readoutReserved), 0});

  buildTabBar(screen);
  if (listCount() == 0) {
    const char* message = showingRecents() ? tr(STR_NO_RECENT_BOOKS) : tr(STR_LIBRARY_NO_RESULTS);
    if (!showingRecents() && filterFailed) {
      message = groupsCollapsed ? tr(STR_LIBRARY_VIEW_UNAVAILABLE) : tr(STR_LIBRARY_SEARCH_UNAVAILABLE);
    } else if (!showingRecents() && !hasFilter()) {
      message = tr(STR_LIBRARY_EMPTY);
    }
    screen.centeredText(message);
    return;
  }
  buildRows(screen);
}

// "12/69 books" at the bottom right: which book is selected, out of how many.
//
// NOT a page count. How many rows fit varies with the view (author headings
// consume band height), so a page total grows and shrinks as you scroll. The
// book position is stable by construction, and it answers the question the
// reader actually has: how far in am I, and how much is left.
void LibraryListActivity::drawPositionReadout() const {
  const int count = listCount();
  if (count <= 0) return;

  char buf[32];
  const char* positionFormat = groupsCollapsed ? tr(STR_LIBRARY_GROUP_POSITION) : tr(STR_LIBRARY_POSITION);
  snprintf(buf, sizeof(buf), positionFormat, selectedEntry() + 1, count);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getTextWidth(SMALL_FONT_ID, buf);
  const int x = renderer.getScreenWidth() - width - SIDE_PADDING;
  const int y = renderer.getScreenHeight() - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, x, y, buf, true);
}

const char* LibraryListActivity::headerTitle() const {
  if (refreshFailed) return tr(STR_LIBRARY_REBUILD_FAILED);
  if (index.limitsReached()) return tr(STR_LIBRARY_PARTIAL);
  if (selectedGroup >= 0) return groupTitle.c_str();
  if (!headerSearchTitle.empty()) return headerSearchTitle.c_str();
  switch (shelfFilter) {
    case library::ShelfFilter::Favorites:
      return tr(STR_LIBRARY_FAVORITES);
    case library::ShelfFilter::Unread:
      return tr(STR_LIBRARY_UNREAD);
    case library::ShelfFilter::Reading:
      return tr(STR_LIBRARY_READING);
    case library::ShelfFilter::Finished:
      return tr(STR_LIBRARY_FINISHED);
    default:
      break;
  }
  return !showingRecents() && degraded ? tr(STR_LIBRARY_TITLE_UNSORTED) : tr(STR_LIBRARY);
}

void LibraryListActivity::drawHoldHelp() const {
  if (mappedInput.hasTouch() || groupsCollapsed) return;
  const char* help = nullptr;
  if (tabsFocused() && !showingRecents() && !degraded)
    help = tr(STR_LIBRARY_HOLD_OPTIONS);
  else if (!tabsFocused())
    help = tr(STR_LIBRARY_HOLD_OPTIONS);
  if (!help) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int y = renderer.getScreenHeight() - metrics.buttonHintsHeight - lineHeight;
  GUI.drawHelpText(renderer, Rect{SIDE_PADDING, y, renderer.getScreenWidth() / 2 - SIDE_PADDING, lineHeight}, help);
}

void LibraryListActivity::drawFooter() {
  drawPositionReadout();
  drawHoldHelp();

  const bool backGoesHome = tabsFocused() && selectedGroup < 0 && (!groupsCollapsed || browsesGroups()) &&
                            (showingRecents() || query.empty());
  const char* backLabel = backGoesHome ? tr(STR_HOME) : tr(STR_BACK);
  const char* confirmLabel = groupsCollapsed ? tr(STR_SELECT) : tr(STR_OPEN);
  const bool canSearch = tabsFocused() && !degraded;
  const auto labels = mappedInput.mapLabels(backLabel, tabsFocused() ? tr(STR_TOGGLE) : confirmLabel,
                                            canSearch ? tr(STR_SEARCH) : tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void LibraryListActivity::optionsActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->openOptions();
}

LibraryListActivity::RefreshSelection LibraryListActivity::captureRefreshSelection() {
  RefreshSelection selection;
  selection.entry = selectedEntry();
  selection.group = selectedGroup >= 0 ? selectedGroup : groupsCollapsed ? selection.entry : -1;
  selection.collapsed = groupsCollapsed;
  selection.inGroup = selectedGroup >= 0;
  selection.tabFocus = tabsFocused();
  if (showingRecents() || !index.isOpen() || (selection.tabFocus && !selection.inGroup)) return selection;
  int first = 0;
  int end = bookRowCount();
  int entry = selection.tabFocus && selection.inGroup ? 0 : selection.entry;
  if (groupsCollapsed) {
    if (!groupStarts || entry < 0 || entry >= groupCount) return selection;
    first = groupStarts[entry];
    end = entry + 1 < groupCount ? groupStarts[entry + 1] : totalBookRowCount();
    entry = first;
  }
  if (entry < first || entry >= end) return selection;
  for (int i = 0; i < 2; ++i) {
    const int anchor = i == 0 ? entry : entry + 1 < end ? entry + 1 : entry - 1;
    if (anchor < first || anchor >= end) continue;
    const int row = rowFor(anchor);
    if (row < 0) continue;
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(row));
    library::ClixRecord record{};
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record) || !index.readPathHash(record, selection.hashes[i]) ||
        !index.readPath(record, selection.paths[i])) {
      selection.paths[i].clear();
      LOG_ERR("LIB", "Cannot remember refresh selection");
    } else {
      selection.sizes[i] = record.fileSize;
    }
  }
  return selection;
}

void LibraryListActivity::restoreRefreshSelection(const RefreshSelection& selection) {
  if (showingRecents()) {
    activeNav().reset(selection.tabFocus || listCount() == 0 ? 0 : std::min(selection.entry, listCount() - 1) + 1);
    return;
  }
  if (!index.isOpen() || filterFailed) return;
  if (selection.tabFocus && !selection.inGroup) return;

  library::PathIdentity identities[2];
  size_t count = 0;
  for (int i = 0; i < 2; ++i) {
    if (!selection.paths[i].empty())
      identities[count++] = {selection.paths[i], selection.hashes[i], selection.sizes[i]};
  }
  uint16_t rows[2] = {0xFFFF, 0xFFFF};
  int match = -1;
  if (count && index.rowsForPaths(sortOrder, identities, count, rows)) {
    for (size_t i = 0; i < count && match < 0; ++i) {
      if (rows[i] == 0xFFFF) continue;
      if (!hasFilter()) {
        match = rows[i];
      } else if (filtered && filteredCount) {
        const auto* found = std::lower_bound(filtered.get(), filtered.get() + filteredCount, rows[i]);
        if (found != filtered.get() + filteredCount && *found == rows[i]) match = found - filtered.get();
      }
    }
  }
  if (browsesGroups()) {
    if (match >= 0 && selection.inGroup) {
      const int group = groupForBook(match);
      expandGroup(group);
      if (selectedGroup == group && !groupsCollapsed) {
        activeNav().reset(selection.tabFocus ? 0 : match - groupStarts[group] + 1);
        return;
      }
    }
    const int group = match >= 0 ? groupForBook(match) : selection.group;
    activeNav().reset(groupCount == 0 ? 0 : std::clamp(group, 0, static_cast<int>(groupCount) - 1) + 1);
  } else if (selection.collapsed && groupable() && collapseGroups(std::max(0, match))) {
    const int group = match >= 0 ? groupForBook(match) : selection.group;
    activeNav().reset(groupCount == 0 ? 0 : std::clamp(group, 0, static_cast<int>(groupCount) - 1) + 1);
  } else {
    const int entry = match >= 0 ? match : selection.entry;
    activeNav().reset(bookRowCount() == 0 ? 0 : std::clamp(entry, 0, bookRowCount() - 1) + 1);
  }
}

void LibraryListActivity::refreshLibrary() {
  const auto selection = captureRefreshSelection();
  library::librarySession.invalidate();
  closeRouting();
  index.close();
  GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
  const auto refreshToken = library::librarySession.refreshToken();
  refreshFailed = !rebuildIndex();
  library::librarySession.reconciled(!refreshFailed, refreshToken);
  if (!index.open(library::libraryIndexPath())) {
    LOG_ERR("LIB", "cannot reopen library index");
    refreshFailed = true;
    library::librarySession.invalidate();
  }
  degraded = index.ranksDegraded();
  applyFilter();
  activeNav().reset();
  restoreRefreshSelection(selection);
  requestUpdate(true);
}

void LibraryListActivity::openOptions() {
  static constexpr StrId OPTIONS[] = {StrId::STR_LIBRARY_GROUPING, StrId::STR_LIBRARY_FILTER,
                                      StrId::STR_LIBRARY_REVERSE, StrId::STR_LIBRARY_REFRESH};
  auto menu = makeUniqueNoThrow<LibraryMenuActivity>(renderer, mappedInput, tr(STR_LIBRARY_OPTIONS), OPTIONS, 4);
  if (!menu) {
    LOG_ERR("LIB", "OOM: library options");
    return;
  }
  app.clearTapFlash();
  const bool reopenIndex = releaseIndexForChild();
  startActivityForResult(std::move(menu), [this, reopenIndex](const ActivityResult& result) {
    RenderLock lock(*this);
    swallowHeldReleases();
    if (!restoreIndexAfterChild(reopenIndex)) return;
    const auto* choice = std::get_if<MenuResult>(&result.data);
    if (result.isCancelled || !choice) return;
    switch (choice->action) {
      case 0:
        openGrouping();
        break;
      case 1:
        openShelfFilter();
        break;
      case 2:
        if (!showingRecents()) toggleSortDirection();
        break;
      case 3:
        refreshLibrary();
        break;
      default:
        break;
    }
  });
}

void LibraryListActivity::openGrouping() {
  static constexpr StrId OPTIONS[] = {StrId::STR_LIBRARY_TAB_TITLE, StrId::STR_LIBRARY_TAB_AUTHOR,
                                      StrId::STR_LIBRARY_SERIES, StrId::STR_LIBRARY_TAB_TIME,
                                      StrId::STR_LIBRARY_TAB_RECENT};
  auto menu = makeUniqueNoThrow<LibraryMenuActivity>(renderer, mappedInput, tr(STR_LIBRARY_GROUPING), OPTIONS, 5);
  if (!menu) {
    LOG_ERR("LIB", "OOM: grouping options");
    return;
  }
  const bool reopenIndex = releaseIndexForChild();
  startActivityForResult(std::move(menu), [this, reopenIndex](const ActivityResult& result) {
    RenderLock lock(*this);
    swallowHeldReleases();
    if (!restoreIndexAfterChild(reopenIndex)) return;
    const auto* choice = std::get_if<MenuResult>(&result.data);
    if (result.isCancelled || !choice) return;
    if (choice->action == 1 || choice->action == 2) {
      const uint8_t series = choice->action == 2;
      if (SETTINGS.libraryGroupBySeries != series) {
        SETTINGS.libraryGroupBySeries = series;
        SETTINGS.saveToFile();
      }
      selectTab(AUTHOR_TAB, false);
    } else if (choice->action == 0)
      selectTab(TITLE_TAB, false);
    else if (choice->action == 3)
      selectTab(ADDED_TAB, false);
    else if (choice->action == 4) {
      shelfFilter = library::ShelfFilter::All;
      query.clear();
      selectTab(RECENT_TAB, false);
    }
  });
}

void LibraryListActivity::openShelfFilter() {
  static constexpr StrId OPTIONS[] = {StrId::STR_LIBRARY_ALL, StrId::STR_LIBRARY_FAVORITES, StrId::STR_LIBRARY_UNREAD,
                                      StrId::STR_LIBRARY_READING, StrId::STR_LIBRARY_FINISHED};
  auto menu = makeUniqueNoThrow<LibraryMenuActivity>(renderer, mappedInput, tr(STR_LIBRARY_FILTER), OPTIONS, 5);
  if (!menu) {
    LOG_ERR("LIB", "OOM: shelf filter options");
    return;
  }
  const bool reopenIndex = releaseIndexForChild();
  startActivityForResult(std::move(menu), [this, reopenIndex](const ActivityResult& result) {
    RenderLock lock(*this);
    swallowHeldReleases();
    if (!restoreIndexAfterChild(reopenIndex)) return;
    const auto* choice = std::get_if<MenuResult>(&result.data);
    if (result.isCancelled || !choice || choice->action < 0 || choice->action > 4) return;
    shelfFilter = static_cast<library::ShelfFilter>(choice->action);
    selectTab(showingRecents() ? ADDED_TAB : activeTabIndex, false);
  });
}

bool LibraryListActivity::resolveBook(const int entry, std::string& path, std::string& title) {
  if (entry < 0 || entry >= bookRowCount()) return false;
  if (showingRecents()) {
    const auto& book = RECENT_BOOKS.getBooks()[static_cast<size_t>(entry)];
    path = book.path;
    title = book.title;
    return true;
  }
  const uint16_t ordinal = index.ordinalForRow(sortOrder, rowFor(entry));
  library::ClixRecord record{};
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record) || !index.readPath(record, path)) return false;
  std::string author;
  return rowTextFor(entry, title, author);
}

void LibraryListActivity::openBookOptions(const int entry) {
  std::string path;
  std::string title;
  if (!resolveBook(entry, path, title)) return;
  library::BookState state;
  const uint64_t key = library::bookStateKey(path);
  if (!library::readBookState(key, state)) {
    GUI.drawPopup(renderer, tr(STR_LIBRARY_STATE_FAILED));
    requestUpdate();
    return;
  }
  const StrId OPTIONS[] = {StrId::STR_OPEN,
                           state.favorite ? StrId::STR_LIBRARY_UNFAVORITE : StrId::STR_LIBRARY_FAVORITE,
                           StrId::STR_LIBRARY_MARK_UNREAD,
                           StrId::STR_LIBRARY_MARK_READING,
                           StrId::STR_LIBRARY_MARK_FINISHED,
                           StrId::STR_DELETE,
                           StrId::STR_LIBRARY_BOOK_DETAILS,
                           showingRecents() ? StrId::STR_REMOVE_FROM_RECENTS : StrId::STR_LIBRARY_GROUPS};
  auto menu = makeUniqueNoThrow<LibraryMenuActivity>(renderer, mappedInput, title, OPTIONS,
                                                     (showingRecents() || groupable()) ? 8 : 7);
  if (!menu) {
    LOG_ERR("LIB", "OOM: book options");
    return;
  }
  app.clearTapFlash();
  const bool reopenIndex = releaseIndexForChild();
  startActivityForResult(std::move(menu), [this, path, title, key, entry, reopenIndex](const ActivityResult& result) {
    RenderLock lock(*this);
    swallowHeldReleases();
    if (!restoreIndexAfterChild(reopenIndex)) return;
    const auto* choice = std::get_if<MenuResult>(&result.data);
    if (result.isCancelled || !choice) return;
    if (choice->action == 0) {
      index.close();
      onSelectBook(path);
      return;
    }
    if (choice->action == 5) {
      promptDeleteBook(path, title);
      return;
    }
    if (choice->action == 6) {
      openBookDetails(path, title);
      return;
    }
    if (choice->action == 7) {
      if (showingRecents())
        promptRemoveRecentBook(path, title);
      else
        collapseGroups(entry);
      return;
    }
    if (choice->action < 1 || choice->action > 4) return;
    library::BookState state;
    if (!library::readBookState(key, state)) {
      GUI.drawPopup(renderer, tr(STR_LIBRARY_STATE_FAILED));
      return;
    }
    if (choice->action == 1)
      state.favorite = !state.favorite;
    else
      state.reading = static_cast<library::ReadingState>(choice->action - 2);
    if (!library::writeBookState(key, state))
      GUI.drawPopup(renderer, tr(STR_LIBRARY_STATE_FAILED));
    else
      refilterAfterBookChange(entry);
    activeNav().followOnBuild = true;
    requestUpdate();
  });
}

void LibraryListActivity::openBookDetails(const std::string& path, const std::string& title) {
  auto details = makeUniqueNoThrow<LibraryBookDetailsActivity>(renderer, mappedInput);
  if (!details || !details->setBook(title, path)) {
    LOG_ERR("LIB", "Cannot open book details");
    GUI.drawPopup(renderer, tr(STR_LIBRARY_VIEW_UNAVAILABLE));
    return;
  }
  const bool reopenIndex = releaseIndexForChild();
  startActivityForResult(std::move(details), [this, path, reopenIndex](const ActivityResult& result) {
    RenderLock lock(*this);
    swallowHeldReleases();
    if (!restoreIndexAfterChild(reopenIndex)) return;
    const auto* choice = std::get_if<MenuResult>(&result.data);
    if (!result.isCancelled && choice && choice->action == 0) {
      index.close();
      onSelectBook(path);
    }
  });
}

void LibraryListActivity::promptDeleteBook(const std::string& path, const std::string& title) {
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: book deletion confirmation");
    return;
  }
  const bool reopenIndex = releaseIndexForChild();
  startActivityForResult(std::move(confirmation), [this, path, reopenIndex](const ActivityResult& result) {
    RenderLock lock(*this);
    swallowHeldReleases();
    if (!restoreIndexAfterChild(reopenIndex)) return;
    if (result.isCancelled) return;
    index.close();
    if (!removeBookFile(path) && Storage.exists(path.c_str())) {
      LOG_ERR("LIB", "cannot delete %s", path.c_str());
      GUI.drawPopup(renderer, tr(STR_LIBRARY_DELETE_FAILED));
      restoreIndexAfterChild(true);
      requestUpdate();
      return;
    }
    RECENT_BOOKS.removeByPath(path);
    refreshLibrary();
  });
}

bool LibraryListActivity::seriesFor(const int entry, std::string& name, uint16_t* position, uint16_t* seriesId) {
  name = tr(STR_LIBRARY_STANDALONE);
  if (position) *position = library::SERIES_INDEX_NONE;
  if (seriesId) *seriesId = library::CLIX_SERIES_NONE;
  if (entry < 0 || entry >= bookRowCount()) return false;
  const uint16_t ordinal = index.ordinalForRow(sortOrder, rowFor(entry));
  library::ClixSeriesRef ref{};
  if (ordinal == 0xFFFF || !index.readSeriesRef(ordinal, ref)) return false;
  if (seriesId) *seriesId = ref.seriesId;
  if (ref.seriesId == library::CLIX_SERIES_NONE) return true;
  uint16_t count = 0;
  if (!index.readSeries(ref.seriesId, name, count)) return false;
  if (position) *position = ref.seriesIndex;
  return true;
}

bool LibraryListActivity::releaseIndexForChild() {
  const bool reopen = index.isOpen();
  index.close();
  return reopen;
}

bool LibraryListActivity::restoreIndexAfterChild(const bool reopen) {
  if (!reopen || index.open(library::libraryIndexPath())) return true;
  LOG_ERR("LIB", "cannot reopen library index after child");
  library::librarySession.invalidate();
  refreshFailed = true;
  applyFilter();
  activeNav().reset();
  requestUpdate();
  return false;
}

void LibraryListActivity::rebuildActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->refreshLibrary();
}
