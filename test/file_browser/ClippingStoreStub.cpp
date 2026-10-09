#include <HalStorage.h>

#include "ClippingStore.h"

// The real store moves its sidecars first; the browser tests only need the book rename.
bool ClippingStore::moveBook(const std::string& from, const std::string& to) {
  return Storage.rename(from.c_str(), to.c_str());
}
