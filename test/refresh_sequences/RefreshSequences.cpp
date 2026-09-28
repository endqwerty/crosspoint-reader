#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

// Same injection seam as the SDK's host/test_pro.cpp; production files stay unmodified.
#define private public
#include "FreeInkDisplay.h"
#include "src/driver/Ssd1677Driver.h"
#include "src/driver/Uc8179Driver.h"
#include "src/driver/Uc8279X4Driver.h"
#undef private
#include "ReaderGrayscalePlan.h"

using namespace freeink;
using Bytes = std::vector<uint8_t>;
using Mode = FreeInkDisplay::RefreshMode;
static std::string context;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s: line %d: %s\n", context.c_str(), __LINE__, #condition); \
      std::exit(1);                                                                     \
    }                                                                                   \
  } while (false)

constexpr unsigned WIDTH_BYTES = 100;
constexpr unsigned HEIGHT = 480;
constexpr unsigned FRAME_BYTES = WIDTH_BYTES * HEIGHT;

// Alternating blank, black, sparse, dense and shifted content exercises both
// erasure directions, byte/row boundaries and the non-visible controller gates.
static Bytes page(unsigned seed) {
  Bytes result(FRAME_BYTES, 0xff);
  for (unsigned y = 0; y < HEIGHT; ++y) {
    for (unsigned x = 0; x < WIDTH_BYTES; ++x) {
      if (seed % 7 == 0)
        result[y * WIDTH_BYTES + x] = 0xff;
      else if (seed % 7 == 1)
        result[y * WIDTH_BYTES + x] = 0;
      else if (seed % 7 == 2)
        result[y * WIDTH_BYTES + x] = (x % 19 == seed % 19 && y % 23 < 5) ? 0x81 : 0xff;
      else
        result[y * WIDTH_BYTES + x] = static_cast<uint8_t>((x * 37 + y * 13 + seed * 23) ^ (y >> 2));
    }
  }
  return result;
}

static Bytes complement(Bytes bytes) {
  for (auto& byte : bytes) byte = static_cast<uint8_t>(~byte);
  return bytes;
}

static Mode scheduled(unsigned turn) {
  if (turn % 50 == 0) return FreeInkDisplay::FULL_REFRESH;
  if (turn % 10 == 0) return FreeInkDisplay::HALF_REFRESH;
  return FreeInkDisplay::FAST_REFRESH;
}

struct Activation {
  Bytes oldPlane;
  Bytes newPlane;
  uint8_t sequence;
  uint8_t control;
  uint8_t cdi;
  bool partial;
  bool externalLut;
};

// This models register retention, never pigment motion or waveform efficacy.
struct Trace {
  bool ssd;
  Bytes oldPlane;
  Bytes newPlane;
  std::array<Bytes, 256> registers;
  std::vector<Activation> activations;
  uint64_t bytes = 0;
  uint64_t planeBytes = 0;
  uint64_t transactions = 0;
  uint64_t waits = 0;
  uint64_t partials = 0;
  uint64_t external = 0;
  uint64_t totalActivations = 0;
  unsigned disabledGrayPages = 0;
  bool partial = false;

  explicit Trace(bool isSsd) : ssd(isSsd) {}

  uint8_t reg(uint8_t command) const { return registers[command].empty() ? 0 : registers[command][0]; }

  void consume(EpdBus& bus) {
    activations.clear();
    waits += bus.waits;
    for (const auto& write : bus.writes) {
      const uint8_t cmd = write.command;
      bytes += 1 + write.bytes.size();
      transactions += write.transactions;
      registers[cmd] = write.bytes;
      if (cmd == (ssd ? 0x26 : 0x10)) {
        oldPlane = write.bytes;
        planeBytes += write.bytes.size();
        CHECK(write.transactions == 1);
      }
      if (cmd == (ssd ? 0x24 : 0x13)) {
        newPlane = write.bytes;
        planeBytes += write.bytes.size();
        CHECK(write.transactions == 1);
      }
      if (!ssd && cmd == 0x91) partial = true;
      if (!ssd && cmd == 0x92) partial = false;
      if (cmd == (ssd ? 0x20 : 0x12)) {
        const bool fast = ssd ? reg(0x22) == 0xfc : partial;
        const bool custom = ssd ? reg(0x22) == 0xcc : (reg(0x00) & 0x20) != 0;
        activations.push_back({oldPlane, newPlane, reg(ssd ? 0x22 : 0xe5), reg(0x21), reg(0x50), fast, custom});
        ++totalActivations;
        partials += fast;
        external += custom;
      }
    }
    bus.clear();
  }

  void report(const std::string& driver, const char* workload, unsigned turns) const {
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
    constexpr const char* buffering = "single";
#else
    constexpr const char* buffering = "dual";
#endif
    std::printf(
        "{\"driver\":\"%s\",\"buffering\":\"%s\",\"workload\":\"%s\",\"turns\":%u,"
        "\"activations\":%llu,\"partial_activations\":%llu,\"external_lut_activations\":%llu,"
        "\"plane_payload_bytes\":%llu,\"all_command_and_data_bytes\":%llu,"
        "\"data_transactions\":%llu,\"wait_calls\":%llu,\"reader_grayscale_disabled_pages\":%u}\n",
        driver.c_str(), buffering, workload, turns, static_cast<unsigned long long>(totalActivations),
        static_cast<unsigned long long>(partials), static_cast<unsigned long long>(external),
        static_cast<unsigned long long>(planeBytes), static_cast<unsigned long long>(bytes),
        static_cast<unsigned long long>(transactions), static_cast<unsigned long long>(waits), disabledGrayPages);
  }
};

template <class Driver>
struct Rig {
  static constexpr bool SSD = std::is_same_v<Driver, Ssd1677Driver>;
  static constexpr bool UC8179 = std::is_same_v<Driver, Uc8179Driver>;
  Driver driver;
  FreeInkDisplay display{12, 11, 13, 18, 14, 6};
  Trace trace{SSD};

  Rig() {
    display._driver = &driver;
    display.begin();
    display._bus.clear();
  }
  ~Rig() {
    display.releaseBuffers();
    if constexpr (!SSD) std::free(driver._grayBase);
  }
  void draw(const Bytes& frame) { std::memcpy(display.getFrameBuffer(), frame.data(), frame.size()); }
  void consume() { trace.consume(display._bus); }

  static Bytes encoded(const Bytes& source, bool inverted = false) {
    if constexpr (SSD) return inverted ? complement(source) : source;
    Bytes output(600 * WIDTH_BYTES, 0xff);
    for (unsigned row = 0; row < HEIGHT; ++row) {
      const unsigned dest = UC8179 ? HEIGHT - row - 1 : row + 120;
      for (unsigned x = 0; x < WIDTH_BYTES; ++x) {
        const uint8_t value = source[row * WIDTH_BYTES + x];
        output[dest * WIDTH_BYTES + x] = inverted ? static_cast<uint8_t>(~value) : value;
      }
    }
    return output;
  }

  void checkBw(const Bytes& submitted, const Bytes& previous, Mode mode, bool cold = false) {
    CHECK(trace.activations.size() == 1);
    const auto& activation = trace.activations.front();
    CHECK(activation.newPlane == encoded(submitted));
    const bool fast = mode == FreeInkDisplay::FAST_REFRESH && !cold;
    if constexpr (SSD) {
      CHECK(activation.sequence == (fast ? 0xfc : mode == FreeInkDisplay::FULL_REFRESH ? 0xf7 : 0xd7));
      CHECK(activation.control == (fast ? 0x00 : 0x40));
      CHECK(activation.oldPlane == encoded(fast ? previous : submitted));
    } else {
      CHECK(activation.sequence == (fast ? 0x5a : 0x1e));
      CHECK(!activation.externalLut);
      CHECK(activation.partial == fast);
      CHECK(activation.oldPlane == (fast                                   ? encoded(previous)
                                    : mode == FreeInkDisplay::HALF_REFRESH ? encoded(submitted, true)
                                                                           : Bytes(600 * WIDTH_BYTES, 0xff)));
      if constexpr (!UC8179) {
        CHECK(activation.cdi == (fast ? 0xd7 : 0x97));
        if (fast) CHECK(trace.registers[0x90] == (Bytes{0x00, 0x00, 0x03, 0x1f, 0x00, 0x78, 0x02, 0x57, 0x01}));
      }
    }
  }
};

template <class Driver>
static void bwSequence(const std::string& name, bool async) {
  Rig<Driver> rig;
  auto previous = page(0);
  for (unsigned turn = 1; turn <= 100; ++turn) {
    context = name + (async ? " asynchronous B/W " : " blocking B/W ") + std::to_string(turn);
    const auto submitted = page(turn);
    rig.draw(submitted);
    const auto mode = scheduled(turn);
    if (async)
      rig.display.displayBufferAsync(mode);
    else
      rig.display.displayBuffer(mode);
    rig.consume();
    rig.checkBw(submitted, previous, mode, turn == 1);
    if (async) {
      CHECK(rig.display.isRefreshPending());
      rig.draw(page(turn + 111));
      rig.display.completeDisplay();
      rig.consume();
      CHECK(rig.trace.activations.empty());
      CHECK(!rig.display.isRefreshPending());
      if constexpr (!Rig<Driver>::SSD) CHECK(rig.trace.oldPlane == rig.encoded(submitted));
      rig.display.completeDisplay();
      CHECK(rig.display._bus.writes.empty());
    } else if constexpr (!Rig<Driver>::SSD)
      CHECK(rig.trace.oldPlane == rig.encoded(submitted));
    previous = submitted;
  }
  CHECK(rig.trace.totalActivations == 100);
  CHECK(rig.trace.partials == 89);
  CHECK(rig.trace.external == 0);
  rig.trace.report(name, async ? "bw_async_periodic_clean" : "bw_blocking_periodic_clean", 100);
}

template <class Driver>
static void aaSequence(const std::string& name, bool alternating) {
  Rig<Driver> rig;
  Bytes previous(FRAME_BYTES, 0xff);
  for (unsigned turn = 1; turn <= 100; ++turn) {
    context = name + " AA " + std::to_string(turn);
    if (alternating && turn % 2 == 0) {
      context += " to B/W";
      const auto next = page(turn);
      rig.draw(next);
      const auto mode = scheduled(turn);
      rig.display.displayBuffer(mode);
      rig.consume();
      if constexpr (Rig<Driver>::SSD)
        rig.checkBw(next, previous, mode);
      else if (mode != FreeInkDisplay::FAST_REFRESH)
        rig.checkBw(next, previous, mode);
      else {
        CHECK(rig.trace.activations.size() == 1);
        const auto& activation = rig.trace.activations.front();
        CHECK(activation.externalLut && activation.partial);
        CHECK(activation.oldPlane == rig.encoded(previous));
        CHECK(activation.newPlane == rig.encoded(next));
      }
      if constexpr (!Rig<Driver>::SSD) CHECK(rig.trace.oldPlane == rig.encoded(next));
      previous = next;
      continue;
    }
    const auto plane0 = page(turn + 2);
    const auto plane1 = page(turn + 4);
    Bytes bw(FRAME_BYTES), lsb(FRAME_BYTES), msb(FRAME_BYTES);
    for (unsigned i = 0; i < FRAME_BYTES; ++i) {
      bw[i] = plane0[i] & plane1[i];
      lsb[i] = plane0[i] & static_cast<uint8_t>(~plane1[i]);
      msb[i] = plane0[i] ^ plane1[i];
    }
    rig.draw(bw);
    const auto mode = scheduled(turn);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
    rig.display.displayGrayscaleBase(mode);
#else
    // The ordinary base API swaps the active buffer used by the next dual-buffer
    // B/W update (the reader's non-combined displayBaseWithRefreshCycle path).
    rig.display.displayBuffer(mode);
#endif
    rig.consume();
    if constexpr (Rig<Driver>::SSD)
      rig.checkBw(bw, previous, mode, turn == 1);
    else if (turn == 1 || mode != FreeInkDisplay::FAST_REFRESH
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
             || alternating
#endif
    )
      rig.checkBw(bw, previous, mode, turn == 1);
    else {
      CHECK(rig.trace.activations.size() == 1);
      const auto& base = rig.trace.activations.front();
      CHECK(base.externalLut && base.partial);
      CHECK(base.oldPlane == rig.encoded(previous));
      CHECK(base.newPlane == rig.encoded(bw));
    }
    // Legacy precondition is intentionally a no-op once the base was painted.
    rig.display.preconditionGrayscale();
    rig.consume();
    CHECK(rig.trace.activations.empty());
    rig.display.copyGrayscaleBuffers(lsb.data(), msb.data());
    rig.consume();
    CHECK(rig.trace.activations.empty());
    if constexpr (Rig<Driver>::SSD) {
      CHECK(rig.trace.newPlane == lsb && rig.trace.oldPlane == msb);
    } else {
      CHECK(rig.trace.oldPlane == rig.encoded(plane0, !Rig<Driver>::UC8179));
      CHECK(rig.trace.newPlane == rig.encoded(plane1, !Rig<Driver>::UC8179));
    }
    rig.display.displayGrayBuffer(false);
    rig.consume();
    CHECK(rig.trace.activations.size() == 1);
    CHECK(rig.trace.activations.front().externalLut);
    if constexpr (!Rig<Driver>::SSD) {
      CHECK(rig.trace.oldPlane == rig.encoded(bw));
      CHECK(rig.trace.newPlane == rig.encoded(bw));
      CHECK(rig.trace.activations.front().cdi == (Rig<Driver>::UC8179 ? 0x29 : 0x97));
      if constexpr (Rig<Driver>::UC8179) {
        auto expected = rig.trace.registers[0x22];
        CHECK(expected.size() == 42);
        expected[2] = 1;
        expected[3] = 2;
        CHECK(rig.trace.registers[0x23] == expected);
      } else {
        unsigned grayPixels = 0;
        for (uint8_t byte : msb) grayPixels += __builtin_popcount(byte);
        if (grayPixels * 100u <= FRAME_BYTES * 8u * 25u) {
          const auto variant = BoardConfig::ACTIVE.displayControllerVariant;
          const uint8_t third = variant == 0x02 || variant == 0x03 ? 2 : 3;
          Bytes expected(49, 0);
          const uint8_t prefix[14] = {1, 2, third, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1};
          std::copy(std::begin(prefix), std::end(prefix), expected.begin());
          CHECK(rig.trace.registers[0x20] == expected);
        }
      }
    }
    rig.display.cleanupGrayscaleBuffers(bw.data());
    rig.consume();
    CHECK(rig.trace.activations.empty());
    CHECK(rig.trace.oldPlane == rig.encoded(bw));
    CHECK(std::memcmp(rig.display.getFrameBuffer(), bw.data(), FRAME_BYTES) == 0);
    previous = bw;
  }
  CHECK(rig.trace.totalActivations == (alternating ? 150 : 200));
  rig.trace.report(name, alternating ? "aa_to_bw_periodic_clean" : "aa_overlay_periodic_clean", 100);
}

template <class Driver>
static void inversionSequence(const std::string& name) {
  Rig<Driver> rig;
  Bytes previous(FRAME_BYTES, 0xff);
  bool inverted = false;
  for (unsigned turn = 1; turn <= 100; ++turn) {
    context = name + " night mode " + std::to_string(turn);
    const bool changed = turn % 5 == 0;
    if (changed) {
      inverted = !inverted;
      rig.display.setInverted(inverted);
      CHECK(rig.display._bus.writes.empty());
    }
    const auto logical = page(turn + 3);
    const auto submitted = inverted ? complement(logical) : logical;
    const auto capabilities = rig.display.grayscaleCapabilities();
    const auto plan = ReaderGrayscalePlan::forPage(true, turn % 3 == 0, capabilities);
    CHECK(capabilities.supported() == !inverted);
    CHECK(plan.enabled == !inverted);
    CHECK(plan.text == !inverted);
    if (inverted) {
      CHECK(!plan.tiled && !plan.overlap && !plan.combinedBase);
      ++rig.trace.disabledGrayPages;
    }
    rig.draw(logical);
    rig.display.displayBuffer(FreeInkDisplay::FAST_REFRESH);
    rig.consume();
    rig.checkBw(submitted, previous, changed ? FreeInkDisplay::HALF_REFRESH : FreeInkDisplay::FAST_REFRESH, turn == 1);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
    CHECK(std::memcmp(rig.display.getFrameBuffer(), logical.data(), FRAME_BYTES) == 0);
#else
    CHECK(std::memcmp(rig.display.frameBufferActive, logical.data(), FRAME_BYTES) == 0);
#endif
    if (inverted) {
      CHECK(!rig.display.supportsAsyncRefresh());
      CHECK(!rig.display.grayscaleCapabilities().supported());
      rig.display.copyGrayscaleBuffers(logical.data(), logical.data());
      rig.display.displayGrayBuffer();
      CHECK(rig.display._bus.writes.empty());
    }
    previous = submitted;
  }
  CHECK(rig.trace.totalActivations == 100);
  CHECK(rig.trace.partials == 79);
  CHECK(rig.trace.disabledGrayPages == 50);
  rig.trace.report(name, "night_mode_toggle_every_five", 100);
}

template <class Driver>
static void overlayRestoreSequence(const std::string& name) {
  Rig<Driver> rig;
  Bytes previous(FRAME_BYTES, 0xff);
  bool inverted = false;
  for (unsigned cycle = 1; cycle <= 100; ++cycle) {
    const bool polarityChanged = cycle % 10 == 0;
    if (polarityChanged) {
      inverted = !inverted;
      rig.display.setInverted(inverted);
    }
    const auto logicalPage = page(cycle + 2);
    const auto submittedPage = inverted ? complement(logicalPage) : logicalPage;
    context = name + " overlay page " + std::to_string(cycle);
    rig.draw(logicalPage);
    rig.display.displayBuffer(FreeInkDisplay::FAST_REFRESH);
    rig.consume();
    rig.checkBw(submittedPage, previous, polarityChanged ? FreeInkDisplay::HALF_REFRESH : FreeInkDisplay::FAST_REFRESH,
                cycle == 1);

    auto logicalChrome = logicalPage;
    // A lower-screen sheet with white background, black border and text marks.
    for (unsigned y = 300; y < HEIGHT; ++y) {
      for (unsigned x = 0; x < WIDTH_BYTES; ++x) {
        logicalChrome[y * WIDTH_BYTES + x] = y == 300 || (y % 17 < 3 && x % 7 < 4) ? 0x00 : 0xff;
      }
    }
    const auto submittedChrome = inverted ? complement(logicalChrome) : logicalChrome;
    context = name + " overlay chrome " + std::to_string(cycle);
    rig.draw(logicalChrome);
    rig.display.displayBuffer(FreeInkDisplay::FAST_REFRESH);
    rig.consume();
    rig.checkBw(submittedChrome, submittedPage, FreeInkDisplay::FAST_REFRESH);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
    CHECK(rig.trace.oldPlane == rig.encoded(submittedChrome));
#else
    CHECK(std::memcmp(rig.display.frameBufferActive, logicalChrome.data(), FRAME_BYTES) == 0);
#endif

    context = name + " overlay restore " + std::to_string(cycle);
    // Equivalent to restoreBwBuffer(false): copy the snapshot into the draw
    // buffer without seeding controller RAM. The next activation must use the
    // shown chrome as OLD (retained in RAM or the active dual buffer).
    rig.draw(logicalPage);
    CHECK(rig.display._bus.writes.empty());
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
    CHECK(rig.trace.oldPlane == rig.encoded(submittedChrome));
#else
    CHECK(std::memcmp(rig.display.frameBufferActive, logicalChrome.data(), FRAME_BYTES) == 0);
#endif
    const auto closeMode = scheduled(cycle);
    rig.display.displayBuffer(closeMode);
    rig.consume();
    rig.checkBw(submittedPage, submittedChrome, closeMode);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
    CHECK(rig.trace.oldPlane == rig.encoded(submittedPage));
#else
    CHECK(std::memcmp(rig.display.frameBufferActive, logicalPage.data(), FRAME_BYTES) == 0);
#endif
    previous = submittedPage;
  }
  CHECK(rig.trace.totalActivations == 300);
  CHECK(rig.trace.external == 0);
  rig.trace.report(name, "bw_overlay_restore_periodic_clean", 300);
}

template <class Driver>
static void directSleepSequence(const std::string& name) {
  Rig<Driver> rig;
  for (unsigned turn = 1; turn <= 100; ++turn) {
    context = name + " Direct cover / B/W / incomplete upload " + std::to_string(turn);
    const auto bw = page(turn), lsb = page(turn + 17), msb = page(turn + 29);
    rig.draw(bw);
    auto* const canvas = rig.display.getFrameBuffer();
    CHECK(rig.display.grayscaleCapabilities(GrayscaleMode::Direct).base == GrayscaleBase::Combined);
    const auto baseMode = scheduled(turn);
    CHECK(rig.display.displayGrayscaleBase(GrayscaleMode::Direct, baseMode, turn % 2 == 0));
    CHECK(rig.display.getFrameBuffer() == canvas);
    CHECK(std::memcmp(canvas, bw.data(), bw.size()) == 0);
    rig.consume();
    if (baseMode == FreeInkDisplay::FULL_REFRESH)
      rig.checkBw(bw, bw, FreeInkDisplay::FULL_REFRESH);
    else
      CHECK(rig.trace.activations.empty());
    rig.display.copyGrayscaleBuffers(lsb.data(), msb.data());
    rig.consume();
    CHECK(rig.trace.activations.empty());
    rig.display.displayGrayBuffer(turn % 2 == 0);
    rig.consume();
    CHECK(rig.trace.activations.size() == 1);
    CHECK(rig.trace.activations[0].oldPlane == rig.encoded(lsb, !Rig<Driver>::UC8179));
    CHECK(rig.trace.activations[0].newPlane == rig.encoded(msb, !Rig<Driver>::UC8179));
    rig.display.cleanupGrayscaleBuffers(bw.data());
    rig.consume();
    CHECK(rig.trace.activations.empty());
    rig.draw(bw);
    rig.display.displayBuffer(FreeInkDisplay::FAST_REFRESH, turn % 2 == 0);
    rig.consume();
    CHECK(rig.trace.activations.size() == 2);
    CHECK(rig.trace.activations.back().newPlane == rig.encoded(bw));
    CHECK(rig.trace.oldPlane == rig.encoded(bw));

    rig.draw(bw);
    CHECK(rig.display.displayGrayscaleBase(GrayscaleMode::Direct));
    rig.display.copyGrayscaleLsbBuffers(lsb.data());
    rig.display.displayGrayBuffer(false);
    rig.consume();
    CHECK(rig.trace.activations.empty());
    rig.display.displayBuffer(FreeInkDisplay::FAST_REFRESH);
    rig.consume();
    CHECK(rig.trace.activations.size() == 1);
    CHECK(!rig.trace.activations.back().partial);
    CHECK(rig.trace.oldPlane == rig.encoded(bw));
  }
  rig.trace.report(name, "direct_cover_abort_bw_recovery", 300);
}

static void windowFallbackSequence(const std::string& name) {
  Rig<Uc8279X4Driver> rig;
  auto previous = page(3);
  for (unsigned turn = 0; turn < 100; ++turn) {
    context = name + " window request uses full-screen B/W fallback " + std::to_string(turn);
    unsigned x = (turn * 24) % 640, y = (turn * 19) % 420, w = 80, h = 23;
    auto submitted = previous;
    for (unsigned row = y; row < y + h; ++row)
      for (unsigned byte = x / 8; byte < (x + w) / 8; ++byte) submitted[row * WIDTH_BYTES + byte] ^= 0x5a;
    bool resync = turn == 0;
    switch (turn % 12) {
      case 1:
        ++x;
        break;
      case 2:
        --w;
        break;
      case 3:
        w = 0;
        break;
      case 4:
        y = 479;
        h = 2;
        break;
      case 5:
        x = 65528;
        w = 16;
        break;
      case 6:
        submitted[(y == 0 ? HEIGHT - 1 : 0) * WIDTH_BYTES] ^= 1;
        break;
      case 7:
        rig.driver.requestResync(1);
        resync = true;
        break;
      case 8:
        rig.driver._redriveAfterGray = true;
        break;
      default:
        break;
    }
    rig.draw(submitted);
    rig.display.displayWindow(x, y, w, h, turn % 2 == 0);
    rig.consume();
    CHECK(rig.trace.activations.size() == 1);
    CHECK(rig.trace.activations[0].newPlane == rig.encoded(submitted));
    CHECK(rig.trace.oldPlane == rig.encoded(submitted));
    CHECK(rig.trace.newPlane == rig.encoded(submitted));
    if (resync) {
      CHECK(!rig.trace.activations[0].partial);
    } else {
      CHECK(rig.trace.activations[0].partial);
      const unsigned x0 = 0, x1 = 799, y0 = 120, y1 = 599;
      const Bytes window = {
          static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0 & 0xf8), static_cast<uint8_t>(x1 >> 8),
          static_cast<uint8_t>(x1 | 7),  static_cast<uint8_t>(y0 >> 8),   static_cast<uint8_t>(y0),
          static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1),        1};
      CHECK(rig.trace.registers[0x90] == window);
    }
    previous = submitted;
  }
  rig.trace.report(name, "full_screen_window_fallback", 100);
}

template <class Driver>
static void run(const std::string& name) {
  bwSequence<Driver>(name, false);
  bwSequence<Driver>(name, true);
  aaSequence<Driver>(name, false);
  aaSequence<Driver>(name, true);
  inversionSequence<Driver>(name);
  overlayRestoreSequence<Driver>(name);
  if constexpr (!Rig<Driver>::SSD) directSleepSequence<Driver>(name);
}

int main() {
  run<Ssd1677Driver>("SSD1677");
  run<Uc8179Driver>("UC8179");
  for (uint8_t variant : {0x02, 0x03, 0x68, 0x69}) {
    BoardConfig::ACTIVE.displayControllerVariant = variant;
    char name[20];
    std::snprintf(name, sizeof(name), "UC8279-0x%02x", variant);
    run<Uc8279X4Driver>(name);
    windowFallbackSequence(name);
  }
}
