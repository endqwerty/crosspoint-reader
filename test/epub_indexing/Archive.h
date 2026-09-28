#pragma once
#include <Print.h>

#include <algorithm>
#include <array>
#include <map>
#include <string>

#include "XmlAllocationFaults.h"
namespace index_test {
inline std::map<std::string, std::string> documents;
inline std::array<bool, 3> streamFailed;
inline xml_fault::Phase phaseFor(const std::string& name) {
  return name.ends_with(".opf")   ? xml_fault::Phase::Opf
         : name.ends_with(".ncx") ? xml_fault::Phase::Ncx
                                  : xml_fault::Phase::Nav;
}
inline bool size(const std::string& name, size_t* bytes) {
  const auto found = documents.find(name);
  if (found == documents.end()) return false;
  *bytes = found->second.size();
  xml_fault::select(phaseFor(name));
  return true;
}
inline bool stream(const std::string& name, Print& target, size_t chunk, bool allowShort = false) {
  const auto found = documents.find(name);
  if (found == documents.end()) return false;
  const auto& bytes = found->second;
  for (size_t at = 0; at < bytes.size(); at += chunk) {
    const size_t count = std::min(chunk, bytes.size() - at);
    if (target.write(reinterpret_cast<const uint8_t*>(bytes.data() + at), count) != count) {
      streamFailed[static_cast<size_t>(phaseFor(name))] = true;
      return allowShort;
    }
  }
  return true;
}
inline bool parserFailed(xml_fault::Phase phase) {
  return streamFailed[static_cast<size_t>(phase)] || xml_fault::stats(phase).setupFailures;
}
}  // namespace index_test
