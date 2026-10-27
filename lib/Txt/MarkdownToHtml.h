#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Streaming Markdown -> XHTML converter behind the TXT reader's rendered .md view.
//
// Fed one source line at a time, it writes well-formed XHTML through `sink` in ~4 KB
// chunks, so only the open paragraph is ever held in RAM. The output is laid out by
// ChapterHtmlSlimParser (the EPUB engine), which is why indentation is expressed as
// inline style="" attributes the parser understands (margin-left, text-indent, ...).
//
// Covered: ATX + setext headings, paragraphs (hard breaks), **bold** / *italic* /
// ***both*** / ~~strike~~, `code`, links (text only), images (alt text), nested
// ordered/unordered/task lists, blockquotes, fenced + indented code, pipe tables,
// horizontal rules, backslash escapes and HTML entities. Raw HTML is shown literally.
class MarkdownToHtml {
 public:
  // Receives converted output; returning false aborts the conversion (e.g. SD write failure).
  struct Sink {
    void* ctx;
    bool (*write)(void* ctx, const char* data, size_t len);
  };

  explicit MarkdownToHtml(const Sink sink) : sink(sink) {}

  bool begin();
  bool feedLine(std::string_view line);
  bool finish();

 private:
  enum class Block : uint8_t { None, Paragraph, ListItem, Quote };
  struct ListLevel {
    int indent;
    bool ordered;
    int nextNumber;
  };

  static constexpr size_t OUT_FLUSH_BYTES = 4 * 1024;
  static constexpr size_t MAX_BLOCK_BYTES = 8 * 1024;
  static constexpr size_t MAX_LIST_DEPTH = 6;
  static constexpr int MAX_TABLE_COLS = 16;

  Sink sink;
  std::string out;
  bool ok = true;

  Block block = Block::None;
  std::string para;
  int paraLines = 0;
  std::string paraFirstLine;
  bool hardBreakPending = false;
  std::string itemMarker;
  std::vector<ListLevel> lists;
  bool blankBefore = true;
  bool gapBeforeNextBlock = false;

  bool inFence = false;
  char fenceChar = 0;
  size_t fenceLen = 0;
  bool inIndentedCode = false;
  bool codeHasLines = false;

  bool inTable = false;
  int tableCols = 0;

  void emit(std::string_view s);
  void flushOut(bool force);

  void appendToBlock(std::string_view text);
  void closeBlock();
  void closeLists();
  void popList();
  void pushList(int indent, bool ordered, int number);
  void openCode();
  void emitCodeLine(std::string_view line);
  void closeCode();
  void emitHeading(int level, std::string_view text);
  void emitTableRow(std::string_view line, bool header);
  void closeTable();
  void blockStyle(const char* base);

  void renderInline(std::string_view s, int depth);
};
