#include "XmlAllocationFaults.h"

#include <expat.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>

namespace {
constexpr size_t PHASES = static_cast<size_t>(xml_fault::Phase::Count);
std::array<xml_fault::Stats, PHASES> statistics;
std::array<size_t, PHASES> failures;
size_t active = 0;
struct alignas(std::max_align_t) Header {
  size_t size, phase;
};
bool refuse(const size_t phase) {
  auto& s = statistics[phase];
  if (++s.calls != failures[phase]) return false;
  ++s.failures;
  return true;
}
void* allocate(const size_t size) {
  if (refuse(active) || size > std::numeric_limits<size_t>::max() - sizeof(Header)) return nullptr;
  auto* block = static_cast<Header*>(std::malloc(sizeof(Header) + size));
  if (!block) return nullptr;
  *block = {size, active};
  statistics[active].liveBytes += size;
  ++statistics[active].liveBlocks;
  return block + 1;
}
void release(void* pointer) {
  if (!pointer) return;
  auto* block = static_cast<Header*>(pointer) - 1;
  statistics[block->phase].liveBytes -= block->size;
  --statistics[block->phase].liveBlocks;
  std::free(block);
}
void* resize(void* pointer, const size_t size) {
  if (!pointer) return allocate(size);
  if (size == 0) {
    release(pointer);
    return nullptr;
  }
  auto* block = static_cast<Header*>(pointer) - 1;
  const size_t phase = block->phase, oldSize = block->size;
  if (refuse(phase) || size > std::numeric_limits<size_t>::max() - sizeof(Header)) return nullptr;
  auto* next = static_cast<Header*>(std::realloc(block, sizeof(Header) + size));
  if (!next) return nullptr;
  next->size = size;
  statistics[phase].liveBytes = statistics[phase].liveBytes - oldSize + size;
  return next + 1;
}
}  // namespace
namespace xml_fault {
void reset() {
  for (const auto& s : statistics)
    if (s.liveBlocks || s.liveBytes) std::abort();
  statistics = {};
  failures = {};
  active = 0;
}
void select(Phase phase) { active = static_cast<size_t>(phase); }
void failAt(Phase phase, size_t call) { failures[static_cast<size_t>(phase)] = call; }
const Stats& stats(Phase phase) { return statistics[static_cast<size_t>(phase)]; }
}  // namespace xml_fault
// Only this executable redirects XML_ParserCreate; the real Expat library is unchanged.
extern "C" XML_Parser XMLCALL testXmlParserCreate(const XML_Char* encoding) {
  const XML_Memory_Handling_Suite allocator{allocate, resize, release};
  auto parser = XML_ParserCreate_MM(encoding, &allocator, nullptr);
  if (!parser) ++statistics[active].setupFailures;
  return parser;
}
