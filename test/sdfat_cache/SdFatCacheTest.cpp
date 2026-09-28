#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "ExFatLib/ExFatLib.h"
#include "FatLib/FatLib.h"
#include "common/FsCache.h"

using Sector = std::array<uint8_t, 512>;
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
struct Disk : FsBlockDeviceInterface {
  std::map<uint32_t, Sector> sectors;
  uint32_t capacity = 32;
  uint32_t failedRead = UINT32_MAX, failedWrite = UINT32_MAX;
  size_t partial = 0, reads = 0, writes = 0, fatReads = 0, dataReads = 0;
  uint32_t fatStart = 0, fatEnd = 0;
  bool atomicRead = false;
  bool isBusy() override { return false; }
  bool syncDevice() override { return true; }
  Sector_t sectorCount() override { return capacity; }
  Sector value(uint32_t n) const {
    auto it = sectors.find(n);
    return it == sectors.end() ? Sector{} : it->second;
  }
  bool readSector(Sector_t n, uint8_t* dst) override {
    ++reads;
    if (n >= fatStart && n < fatEnd)
      ++fatReads;
    else
      ++dataReads;
    if (n >= capacity) return false;
    auto bytes = value(n);
    if (n == failedRead) {
      if (!atomicRead) std::memcpy(dst, bytes.data(), partial);
      return false;
    }
    std::memcpy(dst, bytes.data(), 512);
    return true;
  }
  bool writeSector(Sector_t n, const uint8_t* src) override {
    ++writes;
    if (n >= capacity || n == failedWrite) return false;
    if (std::all_of(src, src + 512, [](uint8_t b) { return b == 0; }))
      sectors.erase(n);
    else
      std::memcpy(sectors[n].data(), src, 512);
    return true;
  }
  bool readSectors(Sector_t n, uint8_t* dst, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!readSector(n + i, dst + 512 * i)) return false;
    return true;
  }
  bool writeSectors(Sector_t n, const uint8_t* src, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!writeSector(n + i, src + 512 * i)) return false;
    return true;
  }
};
Sector pattern(unsigned seed) {
  Sector a{};
  for (size_t i = 0; i < a.size(); ++i) a[i] = (seed + i * 13) % 251 + 1;
  return a;
}
size_t passed = 0, failed = 0;
template <class F>
void check(const char* name, F f) {
  try {
    f();
    ++passed;
  } catch (const std::exception& e) {
    if (failed < 8) std::fprintf(stderr, "FAIL %s: %s\n", name, e.what());
    ++failed;
  }
}
void partialReads(bool atomicRead) {
  for (size_t bytes = 0; bytes <= 512; ++bytes)
    for (int api = 0; api < 3; ++api) {
      check("partial fill then old sector access", [=] {
        Disk d;
        d.atomicRead = atomicRead;
        d.sectors[2] = pattern(3);
        d.sectors[3] = pattern(80);
        FsCache c;
        c.init(&d);
        require(c.prepare(2, FsCache::CACHE_FOR_READ), "initial fill");
        d.failedRead = 3;
        d.partial = bytes;
        require(!c.prepare(3, FsCache::CACHE_FOR_READ), "failed fill accepted");
        d.failedRead = UINT32_MAX;
        if (api == 0) {
          auto* p = c.prepare(2, FsCache::CACHE_FOR_READ);
          require(p && !std::memcmp(p, d.sectors[2].data(), 512), "prepare returned overwritten bytes");
        } else if (api == 1) {
          Sector out{};
          require(c.cacheSafeRead(2, out.data()), "retry read failed");
          require(out == d.sectors[2], "cacheSafeRead returned overwritten bytes");
        } else {
          auto expected = d.sectors[2];
          expected[31] = 0xD2;
          c.setMirrorOffset(8);
          auto* p = c.prepare(2, FsCache::CACHE_FOR_WRITE | FsCache::CACHE_STATUS_MIRROR_FAT);
          require(p, "write prepare failed");
          p[31] = 0xD2;
          require(c.sync(), "write retry failed");
          require(d.value(2) == expected && d.value(10) == expected, "write corrupted untouched bytes or FAT mirror");
        }
      });
    }
}
void writeFailures() {
  for (int mirrorFailure = 0; mirrorFailure < 2; ++mirrorFailure)
    check("dirty write failure retains cache", [=] {
      Disk d;
      d.sectors[2] = pattern(4);
      d.sectors[3] = pattern(5);
      FsCache c;
      c.init(&d);
      c.setMirrorOffset(8);
      auto* p = c.prepare(2, FsCache::CACHE_FOR_WRITE | FsCache::CACHE_STATUS_MIRROR_FAT);
      require(p, "initial dirty fill");
      p[17] = 99;
      Sector expected{};
      std::memcpy(expected.data(), p, 512);
      d.failedWrite = mirrorFailure ? 10 : 2;
      auto reads = d.reads;
      require(!c.prepare(3, FsCache::CACHE_FOR_READ), "failed write-back accepted");
      require(c.isCached(2) && c.isDirty() && d.reads == reads, "dirty data evicted before write-back succeeded");
      require(!std::memcmp(c.cacheBuffer(), expected.data(), 512), "dirty buffer changed");
      d.failedWrite = UINT32_MAX;
      require(c.prepare(3, FsCache::CACHE_FOR_READ), "write-back retry failed");
      require(d.value(2) == expected && d.value(10) == expected, "retry lost primary or mirror bytes");
    });
  check("successful dirty eviction before failed read", [] {
    Disk d;
    d.sectors[2] = pattern(11);
    d.sectors[3] = pattern(12);
    FsCache c;
    c.init(&d);
    c.setMirrorOffset(8);
    auto* p = c.prepare(2, FsCache::CACHE_FOR_WRITE | FsCache::CACHE_STATUS_MIRROR_FAT);
    require(p, "dirty fill");
    p[4] = 100;
    Sector expected{};
    std::memcpy(expected.data(), p, 512);
    d.failedRead = 3;
    d.partial = 512;
    require(!c.prepare(3, FsCache::CACHE_FOR_READ), "failed read accepted");
    require(d.value(2) == expected && d.value(10) == expected, "preceding dirty write lost");
    require(!c.isDirty(), "failed read marked dirty");
  });
  check("reserve skips read and syncs bytes", [] {
    Disk d;
    FsCache c;
    c.init(&d);
    d.failedRead = 2;
    auto* p = c.prepare(2, FsCache::CACHE_RESERVE_FOR_WRITE);
    require(p && d.reads == 0, "reserve read storage");
    auto expected = pattern(9);
    std::memcpy(p, expected.data(), 512);
    require(c.sync() && d.value(2) == expected, "reserved write lost");
  });
}
uint8_t fileByte(size_t offset, unsigned file) { return (offset * 7 + offset / 512 + file * 53) % 251; }
void fileWorkload(uint32_t capacity, unsigned expectedFat, size_t chunks) {
  Disk d;
  d.capacity = capacity;
  Sector scratch{};
  FatFormatter formatter;
  require(formatter.format(&d, scratch.data()), "format failed");
  FatVolume volume;
  require(volume.begin(&d), "mount failed");
  require(volume.fatType() == expectedFat, "wrong filesystem type");
  const size_t cluster = volume.bytesPerCluster();
  d.fatStart = volume.fatStartSector();
  d.fatEnd = d.fatStart + volume.sectorsPerFat() * volume.fatCount();
  std::vector<uint8_t> bytes(cluster);
  {
    FatFile files[2];
    require(files[0].open(&volume, "READA.BIN", O_CREAT | O_RDWR), "open A");
    require(files[1].open(&volume, "READB.BIN", O_CREAT | O_RDWR), "open B");
    for (size_t block = 0; block < chunks; ++block)
      for (unsigned f = 0; f < 2; ++f) {
        for (size_t i = 0; i < cluster; ++i) bytes[i] = fileByte(block * cluster + i, f);
        require(files[f].write(bytes.data(), bytes.size()) == bytes.size(), "fragmented write");
        require(files[f].sync(), "file sync");
      }
    for (auto& f : files) require(f.close(), "close writer");
  }
  FatVolume reopened;
  require(reopened.begin(&d), "remount failed");
  FatFile files[2];
  require(files[0].open(&reopened, "READA.BIN", O_RDONLY), "reopen A");
  require(files[1].open(&reopened, "READB.BIN", O_RDONLY), "reopen B");
  require(files[0].fileSize() == cluster * chunks && files[1].fileSize() == cluster * chunks, "wrong size");
  require(!files[0].isContiguous() && !files[1].isContiguous(), "fixture not fragmented");
  d.reads = d.fatReads = d.dataReads = 0;
  uint64_t digest = 1469598103934665603ULL;
  for (size_t turn = 0; turn < 5; ++turn)
    for (size_t index = 0; index < chunks; ++index)
      for (unsigned f = 0; f < 2; ++f) {
        size_t block = (index * 17 + turn * 7) % chunks;
        size_t offset = block * cluster + 37;
        require(files[f].seekSet(offset), "seek failed");
        std::array<uint8_t, 67> out{};
        require(files[f].read(out.data(), out.size()) == int(out.size()), "small read failed");
        for (size_t i = 0; i < out.size(); ++i) {
          require(out[i] == fileByte(offset + i, f), "small read mismatch");
          digest = (digest ^ out[i]) * 1099511628211ULL;
        }
      }
  std::printf(
      "WORKLOAD fat=%u separate=%d cluster=%zu samples=%zu reads=%zu fat_reads=%zu data_reads=%zu digest=%llu "
      "partition_bytes=%zu\n",
      expectedFat, USE_SEPARATE_FAT_CACHE, cluster, 10 * chunks, d.reads, d.fatReads, d.dataReads,
      (unsigned long long)digest, sizeof(FatPartition));
  // Verify all persisted bytes after the seek-heavy workload, not just sampled glyph-size ranges.
  for (unsigned f = 0; f < 2; ++f) {
    require(files[f].seekSet(0), "rewind failed");
    for (size_t block = 0; block < chunks; ++block) {
      require(files[f].read(bytes.data(), bytes.size()) == int(bytes.size()), "full read failed");
      for (size_t i = 0; i < bytes.size(); ++i)
        require(bytes[i] == fileByte(block * cluster + i, f), "full file mismatch");
    }
    require(files[f].close(), "close reader");
  }
  for (uint32_t i = 0; i < volume.sectorsPerFat(); ++i)
    require(d.value(d.fatStart + i) == d.value(d.fatStart + volume.sectorsPerFat() + i), "FAT copies diverged");
}
void exfatWorkload() {
  Disk d;
  d.capacity = 8388608;
  Sector scratch{};
  ExFatFormatter formatter;
  require(formatter.format(&d, scratch.data()), "exFAT format failed");
  ExFatVolume volume;
  require(volume.begin(&d), "exFAT mount failed");
  const size_t cluster = volume.bytesPerCluster(), chunks = 12;
  std::vector<uint8_t> bytes(cluster);
  {
    ExFatFile files[2];
    require(files[0].open(&volume, "READA.BIN", O_CREAT | O_RDWR), "exFAT open A");
    require(files[1].open(&volume, "READB.BIN", O_CREAT | O_RDWR), "exFAT open B");
    for (size_t block = 0; block < chunks; ++block)
      for (unsigned f = 0; f < 2; ++f) {
        for (size_t i = 0; i < cluster; ++i) bytes[i] = fileByte(block * cluster + i, f);
        require(files[f].write(bytes.data(), bytes.size()) == bytes.size(), "exFAT write");
        require(files[f].sync(), "exFAT sync");
      }
    for (auto& f : files) require(f.close(), "exFAT close writer");
  }
  ExFatVolume reopened;
  require(reopened.begin(&d), "exFAT remount");
  ExFatFile files[2];
  require(files[0].open(&reopened, "READA.BIN", O_RDONLY), "exFAT reopen A");
  require(files[1].open(&reopened, "READB.BIN", O_RDONLY), "exFAT reopen B");
  d.reads = 0;
  uint64_t digest = 1469598103934665603ULL;
  for (size_t index = 0; index < chunks; ++index)
    for (unsigned f = 0; f < 2; ++f) {
      size_t offset = (chunks - index - 1) * cluster + 37;
      require(files[f].seekSet(offset), "exFAT seek");
      std::array<uint8_t, 67> out{};
      require(files[f].read(out.data(), out.size()) == int(out.size()), "exFAT small read");
      for (size_t i = 0; i < out.size(); ++i) {
        require(out[i] == fileByte(offset + i, f), "exFAT small mismatch");
        digest = (digest ^ out[i]) * 1099511628211ULL;
      }
    }
  std::printf("EXFAT separate=%d cluster=%zu reads=%zu digest=%llu\n", USE_SEPARATE_FAT_CACHE, cluster, d.reads,
              (unsigned long long)digest);
  for (unsigned f = 0; f < 2; ++f) {
    require(files[f].seekSet(0), "exFAT rewind");
    for (size_t block = 0; block < chunks; ++block) {
      require(files[f].read(bytes.data(), bytes.size()) == int(bytes.size()), "exFAT full read");
      for (size_t i = 0; i < bytes.size(); ++i)
        require(bytes[i] == fileByte(block * cluster + i, f), "exFAT full mismatch");
    }
    require(files[f].close(), "exFAT close reader");
  }
}
void libraryWorkload(uint32_t capacity, unsigned expectedFat) {
  Disk d;
  d.capacity = capacity;
  Sector scratch{};
  FatFormatter formatter;
  require(formatter.format(&d, scratch.data()), "library format");
  FatVolume volume;
  require(volume.begin(&d), "library mount");
  require(volume.fatType() == expectedFat, "library FAT type");
  require(volume.mkdir("BOOKS"), "create library directory");
  {
    FatFile dir;
    require(dir.open(&volume, "BOOKS", O_RDONLY), "open library directory");
    for (unsigned i = 0; i < 1000; ++i) {
      char name[80];
      std::snprintf(name, sizeof(name), "Library book number %04u.epub", i);
      FatFile f;
      require(f.open(&dir, name, O_CREAT | O_RDWR), "create library entry");
      auto data = pattern(i);
      require(f.write(data.data(), data.size()) == data.size(), "write library entry");
      require(f.close(), "close library entry");
    }
    require(dir.close(), "close library directory");
  }
  FatVolume reopened;
  require(reopened.begin(&d), "library remount");
  FatFile dir;
  require(dir.open(&reopened, "BOOKS", O_RDONLY), "reopen library directory");
  d.reads = d.fatReads = d.dataReads = 0;
  d.fatStart = reopened.fatStartSector();
  d.fatEnd = d.fatStart + reopened.sectorsPerFat() * reopened.fatCount();
  std::array<bool, 1000> seen{};
  size_t count = 0;
  uint64_t digest = 1469598103934665603ULL;
  for (;;) {
    FatFile f;
    if (!f.openNext(&dir, O_RDONLY)) break;
    char name[80]{};
    require(f.getName(name, sizeof(name)) > 0, "library entry name");
    unsigned number = 1000;
    require(std::sscanf(name, "Library book number %u.epub", &number) == 1 && number < 1000, "unexpected entry name");
    char expected[80];
    std::snprintf(expected, sizeof(expected), "Library book number %04u.epub", number);
    require(!std::strcmp(name, expected) && !seen[number], "truncated or duplicate name");
    seen[number] = true;
    ++count;
    require(f.fileSize() == 512, "library size");
    Sector contents{};
    require(f.read(contents.data(), contents.size()) == 512, "library payload read");
    require(contents == pattern(number), "library payload mismatch");
    for (uint8_t b : contents) digest = (digest ^ b) * 1099511628211ULL;
    require(f.close(), "library close");
  }
  require(count == 1000 && std::all_of(seen.begin(), seen.end(), [](bool v) { return v; }), "missing library entries");
  std::printf("LIBRARY fat=%u separate=%d entries=%zu reads=%zu fat_reads=%zu data_reads=%zu digest=%llu\n",
              expectedFat, USE_SEPARATE_FAT_CACHE, count, d.reads, d.fatReads, d.dataReads, (unsigned long long)digest);
  require(dir.close(), "close enumerated library");
}
int main(int argc, char** argv) {
  partialReads(false);
  partialReads(true);
  writeFailures();
  if (argc == 1 || std::string(argv[1]) != "cache-only") {
    check("FAT16 fragmented interleaved files", [] { fileWorkload(131072, 16, 40); });
    check("FAT32 fragmented interleaved files", [] { fileWorkload(8388608, 32, 40); });
    check("FAT16 multiple FAT sectors", [] { fileWorkload(131072, 16, 300); });
    check("FAT32 multiple FAT sectors", [] { fileWorkload(8388608, 32, 300); });
    check("exFAT unchanged behavior", [] { exfatWorkload(); });
    check("FAT16 thousand-book directory", [] { libraryWorkload(131072, 16); });
    check("FAT32 thousand-book directory", [] { libraryWorkload(8388608, 32); });
  }
  std::printf("RESULT passed=%zu failed=%zu separate=%d\n", passed, failed, USE_SEPARATE_FAT_CACHE);
  return failed ? 1 : 0;
}
