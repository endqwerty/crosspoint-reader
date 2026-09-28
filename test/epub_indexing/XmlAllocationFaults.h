#pragma once
#include <cstddef>
namespace xml_fault {
enum class Phase { Opf, Nav, Ncx, Count };
struct Stats {
  size_t calls = 0, failures = 0, liveBytes = 0, liveBlocks = 0, setupFailures = 0;
};
void reset();
void select(Phase phase);
void failAt(Phase phase, size_t call);
const Stats& stats(Phase phase);
}  // namespace xml_fault
