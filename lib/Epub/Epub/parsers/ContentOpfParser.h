#pragma once
#include <ChunkedVector.h>
#include <Print.h>

#include <algorithm>
#include <deque>
#include <optional>
#include <vector>

#include "Epub.h"
#include "expat.h"

class BookMetadataCache;

class ContentOpfParser final : public Print {
  enum ParserState {
    START,
    IN_PACKAGE,
    IN_METADATA,
    IN_BOOK_TITLE,
    IN_BOOK_AUTHOR,
    IN_BOOK_LANGUAGE,
    IN_BOOK_IDENTIFIER,
    IN_META_TEXT,
    IN_MANIFEST,
    IN_SPINE,
    IN_GUIDE,
  };

  // Creators with their file-as and role. A refine is applied to its creator or
  // title directly; only one arriving before its target is staged.
  static constexpr size_t MAX_CREATORS = 8;
  static constexpr size_t MAX_PERSON_REFINES = 8;
  struct StagedCreator {
    std::string id;
    std::string fileAs;
    std::string role;
  };
  struct StagedPersonRefine {
    std::string target;
    std::string value;
    bool isRole = false;
  };

  const std::string& cachePath;
  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  ParserState state = START;
  BookMetadataCache* cache;
  const bool metadataOnly;
  bool metadataComplete = false;
  HalFile tempItemStore;
  std::string coverItemId;
  bool hasExplicitStartReference = false;
  // XML character data is allowed to arrive in several callbacks for one text
  // node (notably around character references). Keep whitespace and creator
  // separation as element state rather than inferring either from callbacks.
  bool metadataSpacePending = false;
  bool authorSeparatorPending = false;
  // Once a glyph does not fit, later XML callbacks must not resume the truncated field.
  bool titleClamped = false;
  bool authorClamped = false;
  bool languageClamped = false;

  StagedCreator creators[MAX_CREATORS];
  size_t creatorCount = 0;
  StagedPersonRefine personRefines[MAX_PERSON_REFINES];
  size_t personRefineCount = 0;
  std::string titleId;
  std::string titleFileAs;
  std::string calibreTitleSort;
  // The <dc:identifier> being read, and the best book UUID seen so far.
  bool identifierIsUuidScheme = false;
  uint8_t uuidRank = 0;

  std::string identifierText;
  std::string identifierScheme;
  std::string metaText;
  std::string metaProperty;
  std::string metaRefines;
  std::string metaId;
  struct CollectionMetadata {
    std::string id;
    std::string title;
    std::optional<float> index;
    bool isSeries = false;
  };
  static constexpr size_t MAX_COLLECTION_CANDIDATES = 8;
  CollectionMetadata collectionCandidates[MAX_COLLECTION_CANDIDATES];
  size_t collectionCount = 0;
  std::string calibreSeries;
  std::optional<float> calibreSeriesIndex;

  // Pick the title and author sort keys from everything staged during <metadata>.
  void resolveSortKeys();
  // Keep the identifier just read if it names the book's UUID more reliably.
  void considerIdentifier();
  // Apply a file-as or role refine to an already-seen title or creator; false
  // when its target has not appeared yet.
  bool applyPersonRefine(const std::string& target, const std::string& value, bool isRole);

  // Index for fast idref→href lookup (binary search over .items.bin)
  struct ItemIndexEntry {
    uint32_t idHash;      // FNV-1a hash of itemId
    uint16_t idLen;       // length for collision reduction
    uint32_t fileOffset;  // offset in .items.bin
  };
  using ItemIndex = ChunkedVector<ItemIndexEntry, 32, 256, 256>;
  std::unique_ptr<ItemIndex> itemIndex;
  bool useItemIndex = false;

  // FNV-1a hash function
  static uint32_t fnvHash(const std::string& s) {
    uint32_t hash = 2166136261u;
    for (char c : s) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 16777619u;
    }
    return hash;
  }

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);

 public:
  std::string title;
  std::string author;
  std::string language;
  std::string isbn;
  std::string asin;
  std::string series;
  std::optional<float> seriesIndex;
  std::string tocNcxPath;
  std::string tocNavPath;  // EPUB 3 nav document path
  std::string coverItemHref;
  std::string guideCoverPageHref;  // Guide reference with type="cover" or "cover-page" (points to XHTML wrapper)
  std::string textReferenceHref;
  std::vector<std::string> cssFiles;  // CSS stylesheet paths
  // Text representation of the selected numeric position for the Library index.
  std::string seriesIndexText;

  // Sort forms curated in Calibre ("Hobbit, The"; "Tolkien, J. R. R."), empty
  // when the book names none. The author sort is the first creator's, and only
  // when that creator is an author.
  std::string titleSort;
  std::string authorSort;
  // The book's UUID in canonical lowercase form, or empty. Calibre stores its
  // library UUID as the uuid-scheme identifier; its "calibre" scheme is not stable.
  std::string uuid;

  explicit ContentOpfParser(const std::string& cachePath, const std::string& baseContentPath, const size_t xmlSize,
                            BookMetadataCache* cache, const bool metadataOnly = false)
      : cachePath(cachePath),
        baseContentPath(baseContentPath),
        remainingSize(xmlSize),
        cache(cache),
        metadataOnly(metadataOnly) {}
  ~ContentOpfParser() override;

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
