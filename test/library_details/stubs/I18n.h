#pragma once
enum StrId { STR_TITLE, STR_LIBRARY_FILE_PATH, STR_LIBRARY_BOOK_DETAILS, STR_BACK, STR_OPEN, STR_DIR_UP, STR_DIR_DOWN };
inline const char* tr(StrId id) {
  switch (id) {
    case STR_TITLE:
      return "Title";
    case STR_LIBRARY_FILE_PATH:
      return "File path";
    case STR_LIBRARY_BOOK_DETAILS:
      return "Book details";
    default:
      return "label";
  }
}
