#pragma once

#include <LibraryText.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Incremental, read-only SD walk. File is HalFile in firmware; the same walker
// runs against an in-memory filesystem in host tests. Only one directory and
// one temporary entry are open at a time, independent of nesting depth.
template <typename File>
class FolderSearch {
 public:
  static constexpr size_t MAX_RESULTS = 256;
  static constexpr size_t MAX_PENDING = 256;
  static constexpr size_t MAX_PATH_BYTES = 1024;
  static constexpr size_t MAX_PENDING_BYTES = 16384;
  static constexpr size_t MAX_RESULT_BYTES = 16384;

  void start(const std::string& root, const std::string& query, const bool hidden) {
    cancel();
    incomplete = false;
    rootPath = root;
    foldedQuery = query;
    showHidden = hidden;
    // Heap-backed, bounded work lists avoid both recursive stack growth and a
    // complete resident SD index. Reuse their allocations across searches.
    pending.reserve(MAX_PENDING);
    results.reserve(MAX_RESULTS);
    pending.emplace_back();
    running = true;
  }

  void cancel() {
    if (directory) directory.close();
    pending.clear();
    results.clear();
    currentPath.clear();
    pendingBytes = resultBytes = 0;
    running = false;
  }

  bool active() const { return running; }
  bool partial() const { return incomplete; }
  std::vector<std::string> takeResults() { return std::move(results); }

  // One directory open, entry read, or directory close per call. The activity
  // checks Back between small batches; no render lock is held during SD I/O.
  // buffer is the browser's existing heap buffer, not a per-entry allocation.
  template <typename StorageType, typename AcceptFile>
  void step(StorageType& storage, char* buffer, const size_t bufferSize, AcceptFile acceptFile) {
    if (!running) return;
    if (!directory) {
      if (pending.empty()) {
        running = false;
        return;
      }
      currentPath = std::move(pending.back());
      pending.pop_back();
      pendingBytes -= currentPath.size();
      directory = storage.open(library::joinLibraryPath(rootPath, currentPath).c_str());
      if (!directory || !directory.isDirectory()) {
        incomplete = true;
        if (directory) directory.close();
      }
      return;
    }

    auto entry = directory.openNextFile();
    if (!entry) {
      if (directory.hasError()) incomplete = true;
      if (directory) directory.close();
      return;
    }
    if (bufferSize < 2) {
      stopIncomplete();
      return;
    }
    buffer[0] = '\0';
    const size_t length = entry.getName(buffer, bufferSize);
    if (length == 0 || length >= bufferSize - 1) {
      incomplete = true;  // Never open a potentially truncated filename.
      return;
    }
    buffer[length] = '\0';
    const std::string_view name{buffer, length};
    if (name == "." || name == ".." || name == "System Volume Information" || (!showHidden && name.front() == '.'))
      return;
    if (name.find_first_of("/\\") != std::string_view::npos) {
      incomplete = true;
      return;
    }
    const bool isDirectory = entry.isDirectory();
    if (!isDirectory && !acceptFile(name)) return;
    if (rootPath.size() + currentPath.size() + name.size() + 2 > MAX_PATH_BYTES) {
      incomplete = true;
      return;
    }
    std::string path = currentPath.empty() ? std::string(name) : library::joinLibraryPath(currentPath, name);
    if (isDirectory) {
      // Queue even when the folder name does not match: its children may do so.
      if (pending.size() >= MAX_PENDING || pendingBytes + path.size() > MAX_PENDING_BYTES) {
        incomplete = true;
      } else {
        pendingBytes += path.size();
        pending.push_back(path);
      }
      path += '/';
    }
    if (!library::matchesQuery(library::fold(name), foldedQuery)) return;
    if (results.size() >= MAX_RESULTS || resultBytes + path.size() > MAX_RESULT_BYTES) {
      stopIncomplete();
      return;
    }
    resultBytes += path.size();
    results.push_back(std::move(path));
  }

 private:
  void stopIncomplete() {
    incomplete = true;
    running = false;
    if (directory) directory.close();
    pending.clear();
    pendingBytes = 0;
  }

  File directory;
  std::string rootPath;
  std::string foldedQuery;
  std::string currentPath;
  std::vector<std::string> pending;
  std::vector<std::string> results;
  size_t pendingBytes = 0;
  size_t resultBytes = 0;
  bool running = false;
  bool incomplete = false;
  bool showHidden = false;
};
