#include "Section.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include "Epub/css/CssParser.h"
#include "Page.h"
#include "SectionPageReader.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v28: text decoration bits now include line-through in serialized wordStyles.
// v29: TextBlock word data stored as one flat arena (offset table + NUL-terminated
// text blob) instead of length-prefixed strings and per-field arrays.
// v30: Arabic shaping changed both drawing and measurement (getTextAdvanceX now
//      measures the shaped visual text); cached word positions from v29 no longer
//      match what drawText renders.
// v32: ImageBlock serializes the book-internal source href after the cache path
//      (lazy extraction: images are header-probed at build time and extracted on
//      first render).
// v33: Support <ruby> and <rt> tags. Skip <rp> tags
// v34: Word gaps are only suppressed for tokens glued in the source, so spaces between
//      Hangul words survive again; ruby element boundaries carry the continuation flag
//      instead. Invalidates v33 caches, whose word positions have the spaces collapsed.

// v34: <br> handling changed layout — a <br> after text is now a margin-stripped
//      line break (browser-like) and only a <br> whose block stays empty injects
//      the scene-break gap, so cached pages laid out by older versions no longer
//      match. Keeps <br>-per-paragraph books (common CJK formatting) from
//      re-adding container spacing at every paragraph.
// v35: Persist a uint32_t visible-text start offset for every page.
// v36: Ruby and CJK justification layout changes invalidate cached word positions.
// v37: Footnote href records grew from 96 to 256 bytes.
// v38: Focus Reading line breaking changed — a visible hyphen/dash inside a word is now a
//      break opportunity, and hyphenation of a focus-split word considers the whole word
//      instead of only its regular-weight suffix. Pages cached by older versions were laid
//      out with the previous, more restrictive break set and no longer match.
// v39: Image top margin is clamped so a full-viewport-height image cannot
//      overflow the page bottom; older caches can hold placements that panels
//      with no bottom inset refuse to draw.
// v40: Ruby groups remain intact when a large text block is soft-flushed.
// v41: Simple HTML table rows are laid out as positioned columns instead of
//      flattened paragraphs with synthetic row/cell labels.
// v42: Closing a block strips inherited vertical margins and padding.
// v43: Paragraph base direction excludes direction changes from inline elements.
// v44: Persist internal-link rectangles with each page for touch navigation.
// v45: Internal EPUB links preserve CSS superscript/subscript positioning.
// v46: Ordered lists number their items, list-style-type: none suppresses markers,
//      and <ul>/<ol> containers contribute their own margins/padding to child insets.
// v47: Word/character spacing; earlier local v47 retained the v46 byte layout.
// v48: Spacing fields distinguish this layout from both earlier version 47 formats.
// v49: Hangul wraps at word spaces and only stretches word gaps, matching upstream v48.
// v50: Paragraph indentation width in the header for cache validation, matching upstream v50.
// v51: Pagebreak markers keep wrapped book text; pagebreak-tagged paragraphs render.
//      Earlier local v50 files lack the indentation field.
// v52: Preserve paragraph continuity and top spacing across soft flushes (upstream v51).
// v53: Missing full-block and black-square symbols now have visible widths (upstream v52).
constexpr uint8_t SECTION_FILE_VERSION = 53;
// Written into the version field while a build is in progress; patched to
// SECTION_FILE_VERSION only when the build is finalized. An abandoned /
// crash-interrupted .bin therefore carries version 0, which loadSectionFile rejects
// as unknown and clears -- so an incomplete file is never mistaken for a valid one.
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
// Written when a build is suspended partway (reader exited or device slept mid-build).
// The file carries valid pages 0..pageCount-1, all LUTs, and a trailer with the parse
// watermark (bytesConsumed, totalBytes) appended after the li LUT. loadSectionFile
// accepts it so a resume shows those pages instantly; the reader extends it by
// rebuilding in the background. Uses the same header layout as SECTION_FILE_VERSION,
// so finalized files are untouched by this feature; older firmware treats the sentinel
// as an unknown version and rebuilds, which is a safe downgrade.
// MUST change in lockstep with SECTION_FILE_VERSION: the sentinel IS the partial's
// format version, so a stale-format partial otherwise passes the header check and
// only fails (noisily, via the block-decode error path) when a page is loaded.
// Derived so the pairing can't be forgotten: 0xFE for v28, 0xFD for v29, ...
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION = 0xFE - (SECTION_FILE_VERSION - 28);
constexpr uint32_t HEADER_SIZE = SectionPageReader::HEADER_SIZE;

struct SectionLookupTables {
  uint16_t pageCount = 0;
  uint32_t offsets[5]{};
};

bool invalidSectionLookup() {
  LOG_ERR("SCT", "Invalid or unreadable section lookup");
  return false;
}

bool readSectionLookup(HalFile& file, SectionLookupTables& tables, const bool allowPartial = true) {
  const uint64_t fileSize = file.size();
  uint8_t version = 0;
  if (fileSize < HEADER_SIZE || !serialization::readPodChecked(file, version)) return invalidSectionLookup();
  const bool partial = version == SECTION_FILE_PARTIAL_VERSION;
  if (version != SECTION_FILE_VERSION && !(allowPartial && partial)) return invalidSectionLookup();
  if (!file.seek(HEADER_SIZE - sizeof(tables.offsets) - sizeof(tables.pageCount)) ||
      !serialization::readPodChecked(file, tables.pageCount) ||
      !serialization::readBytesChecked(file, tables.offsets, sizeof(tables.offsets)))
    return invalidSectionLookup();

  // Page offsets, anchors, paragraphs, list items, then visible offsets share one header.
  const auto* offsets = tables.offsets;
  const uint64_t pageLutEnd = static_cast<uint64_t>(offsets[0]) + tables.pageCount * sizeof(uint32_t);
  const uint64_t paragraphEnd =
      static_cast<uint64_t>(offsets[2]) + sizeof(uint16_t) + tables.pageCount * sizeof(uint16_t);
  const uint64_t listEnd = static_cast<uint64_t>(offsets[3]) + tables.pageCount * sizeof(uint16_t);
  const uint64_t visibleEnd =
      static_cast<uint64_t>(offsets[4]) + tables.pageCount * sizeof(uint32_t) + (partial ? 2 * sizeof(uint32_t) : 0);
  if (offsets[0] < HEADER_SIZE || pageLutEnd > offsets[1] ||
      static_cast<uint64_t>(offsets[1]) + sizeof(uint16_t) > offsets[2] || paragraphEnd != offsets[3] ||
      listEnd != offsets[4] || visibleEnd > fileSize)
    return invalidSectionLookup();
  return true;
}

bool readParagraphLookupCount(HalFile& file, const SectionLookupTables& tables) {
  uint16_t count = 0;
  if (!file.seek(tables.offsets[2]) || !serialization::readPodChecked(file, count) || count != tables.pageCount)
    return invalidSectionLookup();
  return true;
}

std::optional<uint16_t> findPageForIndex(HalFile& file, const uint32_t offset, const uint16_t count,
                                         const uint16_t index) {
  if (!count) return std::nullopt;
  if (!file.seek(offset)) {
    invalidSectionLookup();
    return std::nullopt;
  }
  constexpr uint16_t CHUNK_ENTRIES = 16;
  uint16_t indices[CHUNK_ENTRIES];
  for (uint16_t page = 0; page < count;) {
    const uint16_t chunkCount = std::min<uint16_t>(count - page, CHUNK_ENTRIES);
    if (!serialization::readBytesChecked(file, indices, chunkCount * sizeof(uint16_t))) {
      invalidSectionLookup();
      return std::nullopt;
    }
    for (uint16_t i = 0; i < chunkCount; ++i) {
      if (indices[i] >= index) return page + i;
    }
    page += chunkCount;
  }
  return count - 1;
}

}  // namespace

// Out-of-line so the unique_ptr<ChapterHtmlSlimParser> in BuildContext can be
// constructed/destroyed where the parser's full definition is visible.
Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
    : epub(epub),
      spineIndex(spineIndex),
      renderer(renderer),
      filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}

// Suspend any in-progress build so every section.reset() / navigation / sleep path
// persists the pages already laid out as a partial .bin instead of discarding them
// (no-op once a build has completed or never started).
Section::~Section() {
  prefetchedPage.clear();
  suspendBuild();
}

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!build_ || build_->ioFailed || !file || !page || builtPageCount_ == UINT16_MAX) {
    if (build_) build_->ioFailed = true;
    LOG_ERR("SCT", "Cannot write page %d", builtPageCount_);
    return 0;
  }

  const uint32_t position = file.position();
  if (position < HEADER_SIZE || !page->serialize(file)) {
    build_->ioFailed = true;
    LOG_ERR("SCT", "Failed to serialize page %d", builtPageCount_);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", builtPageCount_);

  builtPageCount_++;
  // pageCount is the pages available to read: a rebuild over a partial only raises it
  // once it has laid out more pages than the partial already covers.
  if (builtPageCount_ > pageCount) {
    pageCount = builtPageCount_;
  }
  return position;
}

void Section::appendPage(std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex,
                         const uint32_t visibleTextOffset) {
  const auto offset = onPageComplete(std::move(page));
  if (offset == 0) {
    if (build_ && build_->parser) build_->parser->markFailed();
    return;
  }
  build_->lut.push_back({offset, paragraphIndex, listItemIndex, visibleTextOffset});
}

bool Section::writeSectionFileHeader(const ReaderRenderSpec& spec) {
  if (!file) {
    LOG_ERR("SCT", "File not open for writing header");
    return false;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(spec.fontId) + sizeof(spec.lineCompression) +
                                   sizeof(spec.extraParagraphSpacing) + sizeof(spec.paragraphIndentSpaces) +
                                   sizeof(spec.paragraphAlignment) + sizeof(spec.viewportWidth) +
                                   sizeof(spec.viewportHeight) + sizeof(pageCount) + sizeof(spec.hyphenationEnabled) +
                                   sizeof(spec.embeddedStyle) + sizeof(spec.imageRendering) +
                                   sizeof(spec.focusReadingEnabled) + sizeof(spec.characterSpacing) +
                                   sizeof(spec.wordSpacingPercent) + sizeof(uint32_t) + sizeof(uint32_t) +
                                   sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t),
                "Header size mismatch");
  // Written as the incomplete sentinel; finalizeBuild() patches it to
  // SECTION_FILE_VERSION as the last step, committing the file.
  if (!(serialization::writePodChecked(file, SECTION_FILE_INCOMPLETE_VERSION) &&
        serialization::writePodChecked(file, spec.fontId) &&
        serialization::writePodChecked(file, spec.lineCompression) &&
        serialization::writePodChecked(file, spec.extraParagraphSpacing) &&
        serialization::writePodChecked(file, spec.paragraphIndentSpaces) &&
        serialization::writePodChecked(file, spec.paragraphAlignment) &&
        serialization::writePodChecked(file, spec.viewportWidth) &&
        serialization::writePodChecked(file, spec.viewportHeight) &&
        serialization::writePodChecked(file, spec.hyphenationEnabled) &&
        serialization::writePodChecked(file, spec.embeddedStyle) &&
        serialization::writePodChecked(file, spec.imageRendering) &&
        serialization::writePodChecked(file, spec.focusReadingEnabled) &&
        serialization::writePodChecked(file, spec.characterSpacing) &&
        serialization::writePodChecked(file, spec.wordSpacingPercent) &&
        serialization::writePodChecked(file, pageCount) &&
        serialization::writePodChecked(file, static_cast<uint32_t>(0)) &&
        serialization::writePodChecked(file, static_cast<uint32_t>(0)) &&
        serialization::writePodChecked(file, static_cast<uint32_t>(0)) &&
        serialization::writePodChecked(file, static_cast<uint32_t>(0)) &&
        serialization::writePodChecked(file, static_cast<uint32_t>(0)))) {
    LOG_ERR("SCT", "Failed to write section header");
    return false;
  }
  return true;
}
bool Section::loadSectionFile(const ReaderRenderSpec& spec) {
  prefetchedPage.clear();
  if (!recoverBuildBackup()) return false;
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  const auto invalidHeader = [this]() {
    LOG_ERR("SCT", "Failed to read section header");
    file.close();
    pageCount = 0;
    partial_ = false;
    partialPageCount_ = 0;
    return false;
  };
  if (file.size() < HEADER_SIZE) return invalidHeader();

  // Match parameters
  bool filePartial = false;
  {
    uint8_t version;
    if (!serialization::readPodChecked(file, version)) return invalidHeader();
    if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }
    filePartial = (version == SECTION_FILE_PARTIAL_VERSION);

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    uint8_t fileParagraphIndentSpaces;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileEmbeddedStyle;
    uint8_t fileImageRendering;
    bool fileFocusReadingEnabled;
    int8_t fileCharacterSpacing = 0;
    uint8_t fileWordSpacingPercent = 0;
    if (!serialization::readPodChecked(file, fileFontId)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileLineCompression)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileExtraParagraphSpacing)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileParagraphIndentSpaces)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileParagraphAlignment)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileViewportWidth)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileViewportHeight)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileHyphenationEnabled)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileEmbeddedStyle)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileImageRendering)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileFocusReadingEnabled)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileCharacterSpacing)) return invalidHeader();
    if (!serialization::readPodChecked(file, fileWordSpacingPercent)) return invalidHeader();

    if (spec.fontId != fileFontId || spec.lineCompression != fileLineCompression ||
        spec.extraParagraphSpacing != fileExtraParagraphSpacing ||
        spec.paragraphIndentSpaces != fileParagraphIndentSpaces || spec.paragraphAlignment != fileParagraphAlignment ||
        spec.viewportWidth != fileViewportWidth || spec.viewportHeight != fileViewportHeight ||
        spec.hyphenationEnabled != fileHyphenationEnabled || spec.embeddedStyle != fileEmbeddedStyle ||
        spec.imageRendering != fileImageRendering || spec.focusReadingEnabled != fileFocusReadingEnabled ||
        spec.characterSpacing != fileCharacterSpacing || spec.wordSpacingPercent != fileWordSpacingPercent) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();
      return false;
    }
  }

  if (!serialization::readPodChecked(file, pageCount)) return invalidHeader();

  if (filePartial) {
    // A partial's pageCount is the watermark of a suspended build. Read the watermark
    // trailer (appended after the visible-offset LUT) so estimatedTotalPages can extrapolate.
    uint32_t liLutOffset = 0;
    if (!file.seek(HEADER_SIZE - sizeof(uint32_t) * 2)) return invalidHeader();
    if (!serialization::readPodChecked(file, liLutOffset)) return invalidHeader();
    uint32_t visibleLutOffset = 0;
    if (!file.seek(HEADER_SIZE - sizeof(uint32_t))) return invalidHeader();
    if (!serialization::readPodChecked(file, visibleLutOffset)) return invalidHeader();
    const uint64_t trailerOffset =
        static_cast<uint64_t>(visibleLutOffset) + static_cast<uint64_t>(pageCount) * sizeof(uint32_t);
    const bool trailerValid = pageCount > 0 && liLutOffset >= HEADER_SIZE && visibleLutOffset > liLutOffset &&
                              trailerOffset + 2 * sizeof(uint32_t) <= file.size();
    if (!trailerValid) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: malformed partial section");
      clearCache();
      pageCount = 0;
      return false;
    }
    if (!file.seek(static_cast<size_t>(trailerOffset))) return invalidHeader();
    if (!serialization::readPodChecked(file, partialBytesConsumed_)) return invalidHeader();
    if (!serialization::readPodChecked(file, partialTotalBytes_)) return invalidHeader();
    partial_ = true;
    partialPageCount_ = pageCount;
  } else {
    partial_ = false;
    partialPageCount_ = 0;
  }

  // Explicit close() required: member variable persists beyond function scope
  file.close();
  LOG_DBG("SCT", "Deserialization succeeded: %d pages%s", pageCount, filePartial ? " (partial)" : "");
  return true;
}

bool Section::clearCache() {
  prefetchedPage.clear();
  const std::string backup = binBackupPath();
  if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache backup");
    return false;
  }
  const std::string tmpBin = binTmpPath();
  if (Storage.exists(tmpBin.c_str())) {
    Storage.remove(tmpBin.c_str());
  }
  if (!Storage.exists(filePath.c_str())) {
    LOG_DBG("SCT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

bool Section::createSectionFile(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  // One-shot build: start, then lay out the whole section in a single pass.
  if (!startBuild(spec, popupFn)) {
    return false;
  }
  if (!buildSomeMore(0)) {  // 0 = build to completion
    return false;
  }
  return buildComplete_;
}

bool Section::startBuild(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  prefetchedPage.clear();
  if (build_) {
    LOG_ERR("SCT", "startBuild called while a build is already active");
    return false;
  }
  if (!recoverBuildBackup()) return false;
  buildComplete_ = false;
  builtPageCount_ = 0;
  // Pages from a loaded partial stay readable (from filePath) while this build writes
  // to the tmp .bin, so availability never drops below the partial's watermark.
  pageCount = partial_ ? partialPageCount_ : 0;

  // Remove a stale tmp .bin from a crash-interrupted build; this build recreates it.
  {
    const std::string staleTmp = binTmpPath();
    if (Storage.exists(staleTmp.c_str())) {
      Storage.remove(staleTmp.c_str());
    }
  }

  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto htmlDir = epub->getCachePath() + "/html";
  const auto htmlPath = htmlDir + "/" + std::to_string(spineIndex) + ".html";
  const auto tmpHtmlPath = htmlDir + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Reuse the previously unzipped HTML if we already have it. The unzipped HTML is keyed only on the
  // book (it lives in the per-book cache dir), not on render settings, so it survives the invalidation
  // that wipes the layout (.bin) caches when font/margin/orientation change -- rebuilds then skip zip
  // inflation entirely. It's promoted by an atomic rename as soon as the inflate succeeds (below), so
  // even a window-only giant spine -- whose .bin never finalizes -- still caches its HTML, letting a
  // reopen skip the multi-second inflate. If htmlPath exists it is known-complete.
  const bool reusedHtml = Storage.exists(htmlPath.c_str());
  bool htmlCached = reusedHtml;
  if (reusedHtml) {
    LOG_DBG("SCT", "Reusing cached HTML %s", htmlPath.c_str());
  } else {
    Storage.mkdir(htmlDir.c_str());

    // Retry logic for SD card timing issues
    bool streamed = false;
    uint32_t fileSize = 0;
    for (int attempt = 0; attempt < 3 && !streamed; attempt++) {
      if (attempt > 0) {
        LOG_DBG("SCT", "Retrying stream (attempt %d)...", attempt + 1);
        delay(50);  // Brief delay before retry
      }

      // Remove any incomplete file from previous attempt before retrying
      if (Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
      }

      HalFile tmpHtml;
      if (!Storage.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
        continue;
      }
      // Larger chunks mean far fewer SD writes inflating the HTML; a 1KB chunk turned a 584KB
      // single-spine novel into ~570 tiny writes (multi-second). 8KB keeps the transient buffers
      // small while cutting the write count 8x.
      streamed = epub->readItemContentsToStream(localPath, tmpHtml, 8192);
      fileSize = tmpHtml.size();
      // Explicitly close() file before calling Storage.remove()
      tmpHtml.close();

      // If streaming failed, remove the incomplete file immediately
      if (!streamed && Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
        LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
      }
    }

    if (!streamed) {
      LOG_ERR("SCT", "Failed to stream item contents to temp file after retries");
      return false;
    }

    LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", tmpHtmlPath.c_str(), fileSize);

    // Promote to the persistent HTML cache immediately -- the inflate is complete and the bytes are
    // valid regardless of whether the layout build finishes, so reopening (even a window-only spine
    // that never finalizes its .bin) skips re-inflation. If the rename fails we just parse the temp.
    if (Storage.rename(tmpHtmlPath.c_str(), htmlPath.c_str())) {
      htmlCached = true;
    } else {
      LOG_DBG("SCT", "Failed to promote HTML cache; parsing from temp");
    }
  }

  if (!Storage.openFileForWrite("SCT", binTmpPath(), file)) {
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  // Header is written with the incomplete-version sentinel; finalizeBuild() commits it.
  if (!writeSectionFileHeader(spec)) {
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }

  auto ctx = makeUniqueNoThrow<BuildContext>();
  if (!ctx) {
    LOG_ERR("SCT", "OOM: BuildContext");
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  // htmlCached == "htmlPath is the live cache" (reused, or just promoted). finalizeBuild/abandonBuild
  // then leave the cached HTML alone; only an un-promoted temp (rename failed) is theirs to clean up.
  // The existing LUT grows only while building; reserve one small batch up front.
  ctx->lut.reserve(32);
  ctx->reusedHtml = htmlCached;
  ctx->htmlPath = htmlPath;
  ctx->tmpHtmlPath = tmpHtmlPath;
  ctx->parsePath = htmlCached ? htmlPath : tmpHtmlPath;

  // Derive the content base directory and image cache path prefix for the parser
  const size_t lastSlash = localPath.find_last_of('/');
  ctx->contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  ctx->imageBasePath = epub->getCachePath() + "/img_" + std::to_string(spineIndex) + "_";

  if (spec.embeddedStyle) {
    ctx->cssParser = epub->getCssParser();
    if (ctx->cssParser) {
      const CssParser::CacheLoadResult cacheResult = ctx->cssParser->loadFromCache();
      if (cacheResult == CssParser::CacheLoadResult::LowMemory) {
        LOG_ERR("SCT", "Insufficient heap to hydrate CSS; section build deferred");
        ctx->cssParser->clear();
        file.close();
        Storage.remove(binTmpPath().c_str());
        if (!ctx->reusedHtml) Storage.remove(ctx->tmpHtmlPath.c_str());
        return false;
      }
      if (cacheResult == CssParser::CacheLoadResult::Invalid) {
        LOG_ERR("SCT", "Failed to load CSS from cache");
      }
    }
  }

  // Collect TOC anchors for this spine so the parser can insert page breaks at chapter boundaries
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    tocAnchors.reserve(8);
    for (int i = startTocIndex; i < epub->getTocItemsCount(); i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) {
        tocAnchors.push_back(std::move(entry.anchor));
      }
    }
  }

  // The parser borrows parsePath, so the context must outlive it. The page callback
  // appends to the active build's LUT for the parser's whole lifetime.
  BuildContext* ctxPtr = ctx.get();
  ctx->parser = makeUniqueNoThrow<ChapterHtmlSlimParser>(
      epub, ctxPtr->parsePath, renderer, spec.fontId, spec.lineCompression, spec.extraParagraphSpacing,
      spec.paragraphAlignment, spec.viewportWidth, spec.viewportHeight, spec.hyphenationEnabled,
      spec.focusReadingEnabled,
      [this](std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex,
             const uint32_t visibleTextOffset) {
        appendPage(std::move(page), paragraphIndex, listItemIndex, visibleTextOffset);
      },
      spec.embeddedStyle, ctxPtr->contentBase, ctxPtr->imageBasePath, spec.imageRendering, std::move(tocAnchors),
      popupFn, ctxPtr->cssParser);
  if (!ctx->parser) {
    LOG_ERR("SCT", "OOM: ChapterHtmlSlimParser");
    if (ctx->cssParser) ctx->cssParser->clear();
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }

  ctx->parser->setTextSpacing(spec.characterSpacing, spec.wordSpacingPercent);
  ctx->parser->setParagraphIndentSpaces(spec.paragraphIndentSpaces);
  Hyphenator::setPreferredLanguage(epub->getLanguage());
  build_ = std::move(ctx);

  if (!build_->parser->beginParse()) {
    LOG_ERR("SCT", "Failed to begin parse");
    abandonBuild();
    return false;
  }
  build_->totalBytes = build_->parser->parseTotalBytes();
  return true;
}

bool Section::buildSomeMore(const int maxPages) {
  if (!build_ || !build_->parser) {
    LOG_ERR("SCT", "buildSomeMore with no active build");
    return false;
  }
  // Pace on pages laid out by THIS build, not pageCount: during a rebuild over a partial,
  // pageCount stays pinned at the partial's watermark until the build passes it, which
  // would otherwise turn one "small" chunk into a blocking rebuild of the whole watermark.
  const int startCount = builtPageCount_;
  for (;;) {
    const auto status = build_->parser->parseStep();
    if (build_->ioFailed || status == ChapterHtmlSlimParser::ParseStatus::Error) {
      LOG_ERR("SCT", "Parse error during incremental build");
      abandonBuild();
      return false;
    }
    if (status == ChapterHtmlSlimParser::ParseStatus::Done) {
      return finalizeBuild();
    }
    // ParseStatus::More: yield once we've laid out the requested number of pages.
    if (maxPages > 0 && (builtPageCount_ - startCount) >= maxPages) {
      build_->bytesConsumed = build_->parser->parseBytesConsumed();
      return true;
    }
  }
}

bool Section::hasHtmlCache() const {
  const std::string htmlPath = epub->getCachePath() + "/html/" + std::to_string(spineIndex) + ".html";
  return Storage.exists(htmlPath.c_str());
}

std::optional<uint16_t> Section::findAnchorDuringBuild(const std::string& anchor) const {
  if (!build_ || !build_->parser) return std::nullopt;
  for (const auto& [key, page] : build_->parser->getAnchors()) {
    if (key == anchor) return page;
  }
  return std::nullopt;
}

std::optional<uint16_t> Section::findAnchor(const std::string& anchor) const {
  if (const auto page = findAnchorDuringBuild(anchor)) {
    return page;
  }
  // Fall back to the on-disk anchor map: a finalized section, or a partial whose map
  // covers everything up to its watermark (nullopt past it -- build further and retry).
  return getPageForAnchor(anchor);
}

uint16_t Section::estimatedTotalPages() const {
  // Extrapolation from a suspended session's watermark trailer. A static snapshot, so no EMA
  // damping is needed. Also the best guess while a rebuild is running but hasn't laid out
  // enough pages yet to extrapolate from its own progress.
  const auto partialEstimate = [this]() -> uint16_t {
    if (!partial_ || partialBytesConsumed_ == 0 || partialTotalBytes_ <= partialBytesConsumed_) {
      return pageCount;
    }
    const uint64_t est = static_cast<uint64_t>(partialPageCount_) * partialTotalBytes_ / partialBytesConsumed_;
    if (est <= pageCount) return pageCount;
    return est > 60000 ? 60000 : static_cast<uint16_t>(est);
  };

  if (!build_) {
    return partial_ ? partialEstimate() : pageCount;  // partial -> extrapolate, finalized -> exact
  }
  const uint32_t consumed = build_->bytesConsumed;
  const uint32_t total = build_->totalBytes;
  if (builtPageCount_ == 0 || consumed == 0 || total <= consumed) return partialEstimate();

  // Raw extrapolation: scale the pages built so far by the fraction of HTML still unparsed. This
  // re-derives from a growing, non-uniform sample, so it jitters up and down as the build crosses
  // dense vs sparse regions of the chapter.
  const uint64_t raw = static_cast<uint64_t>(builtPageCount_) * total / consumed;

  // Damp that jitter with an exponential moving average. Step it once per build advance (keyed on
  // bytesConsumed) rather than per status-bar redraw, so the smoothing rate doesn't depend on how
  // often we repaint. As the build nears the end, consumed -> total and raw -> the built count, so
  // the average settles onto the true count (and finalizeBuild then returns the exact pageCount).
  constexpr float ALPHA = 0.25f;  // weight of each new sample; lower = steadier but slower to settle
  if (build_->smoothedEstimate <= 0) {
    build_->smoothedEstimate = static_cast<float>(raw);  // seed on the first estimate
  } else if (consumed != build_->smoothedAtConsumed) {
    build_->smoothedEstimate += ALPHA * (static_cast<float>(raw) - build_->smoothedEstimate);
  }
  build_->smoothedAtConsumed = consumed;

  const uint64_t est = static_cast<uint64_t>(build_->smoothedEstimate + 0.5f);
  if (est <= pageCount) return pageCount;  // never fewer than the pages already available
  return est > 60000 ? 60000 : static_cast<uint16_t>(est);
}

// A leftover backup denotes interrupted installation or cleanup. Conservatively
// restore the older committed cache; reading progress lives outside this file.
bool Section::recoverBuildBackup() {
  const std::string backup = binBackupPath();
  if (!Storage.exists(backup.c_str())) return true;
  if ((Storage.exists(filePath.c_str()) && !Storage.remove(filePath.c_str())) ||
      !Storage.rename(backup.c_str(), filePath.c_str())) {
    LOG_ERR("SCT", "Previous section cache remains in backup; recovery deferred");
    return false;
  }
  return true;
}

// Write the LUTs and anchor map into the open tmp .bin, patch the header with the built
// page count and table offsets, stamp `version` as the commit point, then swap the tmp
// file over filePath. For SECTION_FILE_PARTIAL_VERSION a watermark trailer
// (bytesConsumed, totalBytes) is appended after the li LUT so a later open can estimate
// the total page count. The parser must still be alive (anchors are read from it).
// On failure, the prior cache stays at filePath or its recoverable backup.
bool Section::commitBuildFile(const uint8_t version, const uint32_t bytesConsumed, const uint32_t totalBytes) {
  const bool asPartial = (version == SECTION_FILE_PARTIAL_VERSION);

  const auto failCommit = [this]() {
    LOG_ERR("SCT", "Failed to commit section cache");
    if (build_) build_->ioFailed = true;
    // Explicit close() required before remove (member variable, O_RDWR handle).
    if (file) file.close();
    Storage.remove(binTmpPath().c_str());
    return false;
  };

  if (!build_ || build_->ioFailed || !file || !build_->parser || build_->parser->hasError() ||
      build_->lut.size() != builtPageCount_)
    return failCommit();

  const uint32_t lutOffset = file.position();
  for (const auto& entry : build_->lut) {
    if (entry.fileOffset == 0) {
      LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
      return failCommit();
    }
    if (!serialization::writePodChecked(file, entry.fileOffset)) return failCommit();
  }

  // Write anchor-to-page map for fragment navigation (e.g. footnote targets). For a
  // partial, skip anchors that landed on the incomplete trailing page the suspend drops.
  const uint32_t anchorMapOffset = file.position();
  const auto& anchors = build_->parser->getAnchors();
  uint16_t anchorCount = 0;
  for (const auto& [anchor, page] : anchors) {
    if (!asPartial || page < builtPageCount_) anchorCount++;
  }
  if (!serialization::writePodChecked(file, anchorCount)) return failCommit();
  for (const auto& [anchor, page] : anchors) {
    if (asPartial && page >= builtPageCount_) continue;
    if (!serialization::writeStringChecked(file, anchor)) return failCommit();
    if (!serialization::writePodChecked(file, page)) return failCommit();
  }

  const uint32_t paragraphLutOffset = file.position();
  if (!serialization::writePodChecked(file, static_cast<uint16_t>(build_->lut.size()))) return failCommit();
  for (const auto& entry : build_->lut) {
    if (!serialization::writePodChecked(file, entry.paragraphIndex)) return failCommit();
  }

  const uint32_t liLutFileOffset = static_cast<uint32_t>(file.position());
  for (const auto& entry : build_->lut) {
    if (!serialization::writePodChecked(file, entry.listItemIndex)) return failCommit();
  }

  const uint32_t visibleLutFileOffset = static_cast<uint32_t>(file.position());
  for (const auto& entry : build_->lut) {
    if (!serialization::writePodChecked(file, entry.visibleTextOffset)) return failCommit();
  }

  if (asPartial) {
    // Watermark trailer, located on load immediately after the visible-offset LUT.
    if (!serialization::writePodChecked(file, bytesConsumed)) return failCommit();
    if (!serialization::writePodChecked(file, totalBytes)) return failCommit();
  }

  // Patch header with the built page count and section offsets...
  if (!file.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(builtPageCount_))) return failCommit();
  if (!serialization::writePodChecked(file, builtPageCount_)) return failCommit();
  if (!serialization::writePodChecked(file, lutOffset)) return failCommit();
  if (!serialization::writePodChecked(file, anchorMapOffset)) return failCommit();
  if (!serialization::writePodChecked(file, paragraphLutOffset)) return failCommit();
  if (!serialization::writePodChecked(file, liLutFileOffset)) return failCommit();
  if (!serialization::writePodChecked(file, visibleLutFileOffset)) return failCommit();
  // ...then commit by overwriting the sentinel version with the real one. Writing the
  // version last makes it the commit point: a crash before here leaves version 0.
  if (!file.seek(0)) return failCommit();
  if (!serialization::writePodChecked(file, version)) return failCommit();
  if (!file.close()) return failCommit();
  if (!recoverBuildBackup()) return failCommit();

  const std::string backup = binBackupPath();
  const bool hadPrevious = Storage.exists(filePath.c_str());
  if (hadPrevious && !Storage.rename(filePath.c_str(), backup.c_str())) return failCommit();
  if (!Storage.rename(binTmpPath().c_str(), filePath.c_str())) {
    if (hadPrevious && !Storage.rename(backup.c_str(), filePath.c_str())) {
      LOG_ERR("SCT", "Cache rollback failed; previous cache retained in backup");
    }
    return failCommit();
  }
  if (hadPrevious && !Storage.remove(backup.c_str())) {
    LOG_ERR("SCT", "Cache backup cleanup deferred");
  }
  return true;
}

bool Section::finalizeBuild() {
  if (!build_ || !build_->parser || !build_->parser->finishParse() || build_->ioFailed) {
    LOG_ERR("SCT", "Failed to finish section parse");
    abandonBuild();
    return false;
  }

  if (!commitBuildFile(SECTION_FILE_VERSION, 0, 0)) {
    abandonBuild();
    return false;
  }
  if (!build_->reusedHtml && !Storage.rename(build_->tmpHtmlPath.c_str(), build_->htmlPath.c_str())) {
    LOG_DBG("SCT", "Failed to promote HTML cache, removing temp");
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  if (build_->cssParser) build_->cssParser->clear();
  build_.reset();
  buildComplete_ = true;
  partial_ = false;
  partialPageCount_ = 0;
  pageCount = builtPageCount_;
  return true;
}

void Section::suspendBuild() {
  prefetchedPage.clear();
  if (!build_) return;
  if (build_->ioFailed || !build_->parser || build_->parser->hasError()) {
    abandonBuild();
    return;
  }

  // Only worth persisting if this build produced pages a pre-existing partial doesn't
  // already cover; otherwise keep the older (bigger) partial and just drop the tmp.
  const bool worthKeeping = builtPageCount_ > 0 && (!partial_ || builtPageCount_ > partialPageCount_);

  bool committed = false;
  if (worthKeeping) {
    // Capture the parse watermark and commit before tearing the parser down (the anchor
    // map is read from it). The incomplete trailing page is intentionally not flushed:
    // only fully laid-out pages are persisted, and the rebuild re-derives the rest.
    const uint32_t consumed = static_cast<uint32_t>(build_->parser->parseBytesConsumed());
    committed = commitBuildFile(SECTION_FILE_PARTIAL_VERSION, consumed, build_->totalBytes);
    if (committed) {
      partial_ = true;
      partialPageCount_ = builtPageCount_;
      partialBytesConsumed_ = consumed;
      partialTotalBytes_ = build_->totalBytes;
      LOG_INF("SCT", "Suspended build: %u pages persisted", builtPageCount_);
    }
  }

  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (!committed && file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
  }
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_.reset();
  buildComplete_ = false;
  pageCount = partial_ ? partialPageCount_ : 0;
  builtPageCount_ = 0;
}

void Section::abandonBuild() {
  prefetchedPage.clear();
  if (!build_) return;
  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
  }
  if (Storage.exists(binTmpPath().c_str())) Storage.remove(binTmpPath().c_str());
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_.reset();
  buildComplete_ = false;
  pageCount = partial_ ? partialPageCount_ : 0;
  builtPageCount_ = 0;
}

std::unique_ptr<Page> Section::loadPageDuringBuild(const int page) {
  if (!build_ || build_->ioFailed || page < 0 || page >= static_cast<int>(build_->lut.size()) || !file) return nullptr;
  const uint32_t pos = build_->lut[page].fileOffset;
  if (pos == 0) return nullptr;
  const uint32_t writePos = file.position();
  if (!file.seek(pos)) {
    LOG_ERR("SCT", "Failed to seek built page");
    build_->ioFailed = true;
    file.close();
    return nullptr;
  }
  auto pageData = Page::deserialize(file);
  if (!file.seek(writePos) || !pageData) {
    LOG_ERR("SCT", "Failed to read built page or restore write cursor");
    build_->ioFailed = true;
    file.close();
    return nullptr;
  }
  pageData->visibleTextOffset = build_->lut[page].visibleTextOffset;
  return pageData;
}

// Read a page from the committed file at filePath (finalized section or partial from a
// previous session). Uses a local handle so it is safe while a build holds the member
// `file` open on the tmp .bin.
std::unique_ptr<Page> Section::loadPageAt(const int page) const { return SectionPageReader::load(filePath, page); }

bool Section::retainPrefetchedPage(const int page, std::unique_ptr<Page> decoded, const size_t freeHeap,
                                   const size_t largestBlock) {
  if (build_ || page < 0 || page >= pageCount) {
    prefetchedPage.clear();
    return false;
  }
  return prefetchedPage.retain(page, std::move(decoded), freeHeap, largestBlock);
}

std::unique_ptr<Page> Section::loadPage(const int page) {
  auto decoded = prefetchedPage.take(page);
  if (decoded && !build_ && page >= 0 && page < pageCount) return decoded;
  decoded.reset();
  if (page < 0) {
    return nullptr;
  }
  if (build_ && page < static_cast<int>(build_->lut.size())) {
    return loadPageDuringBuild(page);
  }
  // Not (yet) in the active build: serve from the file on disk -- a finalized section,
  // or a partial from a previous session whose pages the rebuild hasn't reached again.
  const int onDisk = partial_ ? partialPageCount_ : (build_ ? 0 : pageCount);
  if (page >= onDisk) {
    return nullptr;
  }
  return loadPageAt(page);
}

std::string Section::getTextFromSectionFile() {
  std::string fullText;
  auto p = loadPage(currentPage);
  if (p) {
    for (const auto& el : p->elements) {
      if (el->getTag() == TAG_PageLine) {
        const auto& line = static_cast<const PageLine&>(*el);
        if (line.getBlock()) {
          const auto& block = *line.getBlock();
          for (uint16_t i = 0; i < block.wordCount(); i++) {
            if (!fullText.empty()) fullText += " ";
            fullText += block.wordText(i);
          }
        }
      }
    }
  }
  return fullText;
}

std::optional<uint16_t> Section::getCachedPageCount() const {
  HalFile file;
  SectionLookupTables tables;
  // Partial counts are watermarks, not chapter totals.
  if (!Storage.openFileForRead("SCT", filePath, file) || !readSectionLookup(file, tables, false)) return std::nullopt;
  return tables.pageCount;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile file;
  SectionLookupTables tables;
  if (!Storage.openFileForRead("SCT", filePath, file) || !readSectionLookup(file, tables)) return std::nullopt;
  const auto invalidLookup = []() -> std::optional<uint16_t> {
    invalidSectionLookup();
    return std::nullopt;
  };
  const uint32_t end = tables.offsets[2];
  uint16_t count = 0;
  uint32_t position = tables.offsets[1] + sizeof(count);
  if (!file.seek(tables.offsets[1]) || !serialization::readPodChecked(file, count) ||
      static_cast<uint64_t>(position) + count * (sizeof(uint32_t) + sizeof(uint16_t)) > end)
    return invalidLookup();

  // Compare against the caller's name without allocating from an on-card length.
  char chunk[64];
  for (uint16_t i = 0; i < count; ++i) {
    uint32_t length = 0;
    if (static_cast<uint64_t>(position) + sizeof(length) > end || !serialization::readPodChecked(file, length))
      return invalidLookup();
    position += sizeof(length);
    if (static_cast<uint64_t>(position) + length + sizeof(uint16_t) > end) return invalidLookup();
    bool matches = length == anchor.size();
    if (!matches) {
      if (!file.seek(position + length)) return invalidLookup();
    } else {
      for (uint32_t compared = 0; compared < length;) {
        const auto bytes = static_cast<size_t>(std::min<uint32_t>(length - compared, sizeof(chunk)));
        if (!serialization::readBytesChecked(file, chunk, bytes)) return invalidLookup();
        if (matches && memcmp(chunk, anchor.data() + compared, bytes) != 0) matches = false;
        compared += bytes;
      }
    }
    uint16_t page = 0;
    if (!serialization::readPodChecked(file, page) || page >= tables.pageCount) return invalidLookup();
    position += length + sizeof(page);
    if (matches) return page;
  }
  return std::nullopt;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  HalFile file;
  SectionLookupTables tables;
  if (!Storage.openFileForRead("SCT", filePath, file) || !readSectionLookup(file, tables) ||
      !readParagraphLookupCount(file, tables))
    return std::nullopt;
  return findPageForIndex(file, tables.offsets[2] + sizeof(uint16_t), tables.pageCount, pIndex);
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  HalFile file;
  SectionLookupTables tables;
  if (!Storage.openFileForRead("SCT", filePath, file) || !readSectionLookup(file, tables) || page >= tables.pageCount ||
      !readParagraphLookupCount(file, tables))
    return std::nullopt;
  uint16_t index = 0;
  if (!file.seek(tables.offsets[2] + sizeof(uint16_t) + page * sizeof(uint16_t)) ||
      !serialization::readPodChecked(file, index)) {
    invalidSectionLookup();
    return std::nullopt;
  }
  return index;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  HalFile file;
  SectionLookupTables tables;
  if (!Storage.openFileForRead("SCT", filePath, file) || !readSectionLookup(file, tables) ||
      !readParagraphLookupCount(file, tables))
    return std::nullopt;
  return findPageForIndex(file, tables.offsets[3], tables.pageCount, liIndex);
}

std::optional<uint32_t> Section::getVisibleTextOffsetForPage(const uint16_t page) const {
  if (build_ && page < build_->lut.size()) {
    return build_->lut[page].visibleTextOffset;
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  const auto invalidLookup = []() -> std::optional<uint32_t> {
    LOG_ERR("SCT", "Failed to read page visible offset");
    return std::nullopt;
  };
  uint8_t version = 0;
  if (!serialization::readPodChecked(f, version)) return invalidLookup();
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }

  uint16_t count = 0;
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t)) || !serialization::readPodChecked(f, count)) {
    return invalidLookup();
  }
  if (page >= count) {
    return std::nullopt;
  }

  uint32_t visibleLutOffset = 0;
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t)) || !serialization::readPodChecked(f, visibleLutOffset)) {
    return invalidLookup();
  }
  const uint64_t lutEnd = static_cast<uint64_t>(visibleLutOffset) + static_cast<uint64_t>(count) * sizeof(uint32_t);
  const uint64_t entryOffset = static_cast<uint64_t>(visibleLutOffset) + static_cast<uint64_t>(page) * sizeof(uint32_t);
  if (visibleLutOffset < HEADER_SIZE || lutEnd > f.size() || entryOffset + sizeof(uint32_t) > lutEnd) {
    return invalidLookup();
  }

  uint32_t result = 0;
  if (!f.seek(static_cast<size_t>(entryOffset)) || !serialization::readPodChecked(f, result)) return invalidLookup();

  return result;
}

std::optional<uint16_t> Section::getPageForVisibleTextOffset(const uint32_t offset,
                                                             const bool preferFirstAtOffset) const {
  const auto findInEntries = [offset, preferFirstAtOffset](const auto& entries) -> std::optional<uint16_t> {
    if (entries.empty()) return std::nullopt;
    uint16_t result = 0;
    for (size_t i = 0; i < entries.size(); i++) {
      const uint32_t pageStart = entries[i].visibleTextOffset;
      if (preferFirstAtOffset && pageStart == offset) {
        return static_cast<uint16_t>(i);
      }
      if (pageStart > offset) break;
      result = static_cast<uint16_t>(i);
    }
    return result;
  };

  if (build_ && !build_->lut.empty()) {
    // Resolve within the active build's known range. Later offsets may still be
    // covered by an on-disk partial that the resumed build has not reached yet.
    if (offset <= build_->lut.back().visibleTextOffset) {
      return findInEntries(build_->lut);
    }
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  const auto invalidLookup = []() -> std::optional<uint16_t> {
    LOG_ERR("SCT", "Failed to read visible-offset lookup");
    return std::nullopt;
  };
  uint8_t version = 0;
  if (!serialization::readPodChecked(f, version)) return invalidLookup();
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }
  const bool partial = version == SECTION_FILE_PARTIAL_VERSION;

  uint16_t count = 0;
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t)) || !serialization::readPodChecked(f, count)) {
    return invalidLookup();
  }
  if (count == 0) {
    return std::nullopt;
  }

  uint32_t visibleLutOffset = 0;
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t)) || !serialization::readPodChecked(f, visibleLutOffset)) {
    return invalidLookup();
  }
  const uint64_t lutEnd = static_cast<uint64_t>(visibleLutOffset) + static_cast<uint64_t>(count) * sizeof(uint32_t);
  if (visibleLutOffset < HEADER_SIZE || lutEnd > f.size()) return invalidLookup();

  if (!f.seek(visibleLutOffset)) return invalidLookup();
  uint16_t result = 0;
  uint32_t lastPageStart = 0;
  constexpr uint16_t CHUNK_ENTRIES = 16;
  uint32_t pageStarts[CHUNK_ENTRIES];
  for (uint16_t page = 0; page < count;) {
    const uint16_t chunkCount = std::min<uint16_t>(count - page, CHUNK_ENTRIES);
    if (!serialization::readBytesChecked(f, pageStarts, chunkCount * sizeof(uint32_t))) return invalidLookup();
    for (uint16_t i = 0; i < chunkCount; ++i) {
      const uint32_t pageStart = pageStarts[i];
      lastPageStart = pageStart;
      if (preferFirstAtOffset && pageStart == offset) return page + i;
      if (pageStart > offset) return result;
      result = page + i;
    }
    page += chunkCount;
  }
  if (partial && offset > lastPageStart) {
    return std::nullopt;
  }
  return result;
}
