#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>
#include <limits>

#include "AtomicFile.h"

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  if (doc.overflowed()) {
    LOG_ERR("PERSIST", "Incomplete JSON for %s", path);
    return false;
  }
  const size_t expected = measureJson(doc);
  if (expected > atomic_file::MAX_FILE_BYTES) {
    LOG_ERR("PERSIST", "Document exceeds %zu bytes: %s", atomic_file::MAX_FILE_BYTES, path);
    return false;
  }
  Storage.mkdir("/.crosspoint");
  String json;
  if (serializeJson(doc, json) != expected || json.length() != expected ||
      !atomic_file::write(path, json.c_str(), json.length())) {
    LOG_ERR("PERSIST", "Failed to write %s", path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc, bool* exists) {
  bool found = false;
  String json = atomic_file::read(path, &found);
  if (exists) *exists = found;
  if (!found) {
    return false;  // Expected on first boot — not an error.
  }
  if (json.isEmpty()) {
    LOG_ERR("PERSIST", "Failed to read %s (empty)", path);
    return false;
  }
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", path, error.c_str());
    return false;
  }
  return true;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool valid = false;
  return extractPassword(doc, needsResave, std::numeric_limits<size_t>::max(), valid);
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave, const size_t maxLength,
                                                  bool& valid) {
  valid = true;
  bool ok = false;
  bool tooLong = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", maxLength, &ok, &tooLong);
  if (tooLong) {
    valid = false;
    return "";
  }
  if (!ok) {
    // Deobfuscation failed — fall back to legacy plaintext password.
    const char* legacyPassword = doc["password"] | "";
    const size_t legacyLength = strlen(legacyPassword);
    if (legacyLength > maxLength) {
      valid = false;
      return "";
    }
    pass.assign(legacyPassword, legacyLength);
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
