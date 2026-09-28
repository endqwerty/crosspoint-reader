#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <vector>

#include "PageTurnFixture.h"

namespace {
using namespace epub_page_test;
using WorkMember = Work TurnResult::*;

bool sameCounters(const Work& left, const Work& right) {
  return std::tie(left.io.opens, left.io.seeks, left.io.reads, left.io.bytes, left.allocations.calls,
                  left.allocations.bytes, left.deserializations, left.renderPasses, left.bwRefreshes,
                  left.grayRefreshes) == std::tie(right.io.opens, right.io.seeks, right.io.reads, right.io.bytes,
                                                  right.allocations.calls, right.allocations.bytes,
                                                  right.deserializations, right.renderPasses, right.bwRefreshes,
                                                  right.grayRefreshes);
}

bool measure(Scene scene, int samples) {
  PageTurnFixture fixture(scene);
  constexpr size_t PHASES = 3;
  constexpr WorkMember MEMBERS[] = {&TurnResult::idle, &TurnResult::foregroundLoad, &TurnResult::foregroundRender};
  constexpr const char* PHASE_NAMES[] = {"idle_load_and_prewarm", "foreground_page_acquisition", "foreground_render"};
  std::array<std::vector<double>, PHASES * 2> times;
  std::array<Work, PHASES * 2> stats;
  size_t budgetBytes[2] = {};
  for (auto& phase : times) phase.reserve(samples);
  const auto expected = fixture.run(false, GfxRenderer::Portrait);
  for (int sample = -1; sample < samples; ++sample) {
    for (int trial = 0; trial < 2; ++trial) {
      const int variant = (sample + trial + 1) % 2;
      const auto result = fixture.run(variant != 0, GfxRenderer::Portrait);
      if (result.serializedPage != fixture.expectedBody() || result.bw != expected.bw || result.lsb != expected.lsb ||
          result.msb != expected.msb || result.visibleTextOffset != expected.visibleTextOffset) {
        std::fprintf(stderr, "Page integrity mismatch\n");
        return false;
      }
      budgetBytes[variant] = result.retainedBudgetBytes;
      for (size_t phase = 0; phase < PHASES; ++phase) {
        const size_t index = variant * PHASES + phase;
        const auto& work = result.*MEMBERS[phase];
        if (sample >= 0) {
          if (!sameCounters(stats[index], work)) {
            std::fprintf(stderr, "Nondeterministic operation counts in %s/%s\n", sceneName(scene), PHASE_NAMES[phase]);
            return false;
          }
          times[index].push_back(work.hostUs);
        }
        stats[index] = work;
      }
    }
  }
  for (int variant = 0; variant < 2; ++variant) {
    for (size_t phase = 0; phase < PHASES; ++phase) {
      const size_t index = variant * PHASES + phase;
      auto& values = times[index];
      std::sort(values.begin(), values.end());
      const size_t half = values.size() / 2;
      const double median = values.size() % 2 ? values[half] : (values[half - 1] + values[half]) / 2;
      const auto& work = stats[index];
      std::printf("%s,%s,%s,%d,%.3f,%zu,%zu,%zu,%zu,%zu,%zu,%u,%u,%u,%u,%zu\n", sceneName(scene),
                  variant ? "retain" : "reload", PHASE_NAMES[phase], samples, median, work.io.opens, work.io.seeks,
                  work.io.reads, work.io.bytes, work.allocations.calls, work.allocations.bytes, work.deserializations,
                  work.renderPasses, work.bwRefreshes, work.grayRefreshes, budgetBytes[variant]);
    }
  }
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  int samples = 21;
  if (argc == 3 && std::strcmp(argv[1], "--samples") == 0) {
    const auto* end = argv[2] + std::strlen(argv[2]);
    const auto parsed = std::from_chars(argv[2], end, samples);
    if (parsed.ec != std::errc{} || parsed.ptr != end || samples < 3 || samples > 1001) return 2;
  } else if (argc != 1) {
    std::fprintf(stderr, "Usage: EpubPageTurnBenchmark [--samples 3..1001]\n");
    return 2;
  }
  std::puts("# Production Page/TextBlock serialize/deserialize/render + real font prewarm and BW/gray composition.");
  std::puts(
      "# Host CPU only; in-memory section-file fixture with real LUT loader; excludes SD latency, UI, SPI and panel "
      "BUSY.");
  std::puts("# C++ new/new[] requests only, excluding C malloc, allocator overhead, live heap and fragmentation.");
  std::puts(
      "scene,policy,phase,samples,host_median_us,opens,seeks,reads,bytes,cpp_allocations,cpp_requested_bytes,"
      "page_deserializations,page_render_passes,bw_refresh_calls,gray_refresh_calls,retained_budget_charge");
  return measure(Scene::Prose, samples) && measure(Scene::Annotated, samples) ? 0 : 1;
}
