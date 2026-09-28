#pragma once

#include <EpubSearch.h>

#include <memory>
#include <string>

#include "activities/UiListActivity.h"

class Epub;

class EpubSearchActivity final : public UiListActivity {
 public:
  EpubSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::shared_ptr<Epub>& epub);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return searching; }

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;

 private:
  struct Rows {
    freeink::ui::ListItem items[epub_search::MAX_RESULTS] = {};
    char sections[epub_search::MAX_RESULTS][32] = {};
  };

  static bool cancelScan(void* context);
  void scanChapter();
  void finishSearch();
  void cancelSearch();

  std::shared_ptr<Epub> epub;
  std::unique_ptr<epub_search::Results> results;
  std::unique_ptr<Rows> rows;
  std::string query;
  int nextSpine = 0;
  int spineCount = 0;
  uint32_t searchedBytes = 0;
  bool waitingForQuery = true;
  bool searching = false;
  bool showSearchProgress = false;
  bool partial = false;
  bool failed = false;
  bool invalidQuery = false;
  bool cancelled = false;
  bool goHomeRequested = false;
};
