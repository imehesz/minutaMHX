#include "MarkdownToHtml.h"

#include <cstdio>
#include <cstring>
#include <iterator>

#include "Epub/htmlEntities.h"

namespace {

constexpr size_t NPOS = std::string_view::npos;
constexpr int MAX_INLINE_DEPTH = 8;
// Bullet glyphs per nesting level; all are in the built-in reader fonts (U+2022, U+2013).
constexpr const char* BULLETS[] = {"\xe2\x80\xa2", "\xe2\x80\x93"};
constexpr const char* NBSP = "&#160;";

bool isSpace(const char c) { return c == ' ' || c == '\t'; }
bool isAlnum(const char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (static_cast<unsigned char>(c) >= 0x80);
}
bool isAsciiPunct(const char c) {
  return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
  while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
  return s;
}

size_t runLength(const std::string_view s, const size_t pos, const char c) {
  size_t n = 0;
  while (pos + n < s.size() && s[pos + n] == c) n++;
  return n;
}

// Leading indentation in columns (tab = next multiple of 4) and the byte offset where content starts.
int measureIndent(const std::string_view line, size_t& contentStart) {
  int cols = 0;
  size_t i = 0;
  while (i < line.size() && isSpace(line[i])) {
    cols = line[i] == '\t' ? (cols / 4 + 1) * 4 : cols + 1;
    i++;
  }
  contentStart = i;
  return cols;
}

bool allChar(const std::string_view s, const char c) {
  if (s.empty()) return false;
  for (const char ch : s) {
    if (ch != c) return false;
  }
  return true;
}

bool isHorizontalRule(const std::string_view rest) {
  if (rest.empty() || (rest[0] != '-' && rest[0] != '*' && rest[0] != '_')) return false;
  int count = 0;
  for (const char c : rest) {
    if (c == rest[0]) {
      count++;
    } else if (!isSpace(c)) {
      return false;
    }
  }
  return count >= 3;
}

// |---|:--:| style delimiter row.
bool isTableSeparator(const std::string_view rest) {
  bool sawDash = false;
  bool sawPipe = false;
  for (const char c : rest) {
    if (c == '-') {
      sawDash = true;
    } else if (c == '|') {
      sawPipe = true;
    } else if (c != ':' && !isSpace(c)) {
      return false;
    }
  }
  return sawDash && sawPipe;
}

// Splits a table row on unescaped pipes outside code spans; the outer pipes are optional.
int splitTableRow(std::string_view row, std::string_view* cells, const int maxCells) {
  row = trim(row);
  if (!row.empty() && row.front() == '|') row.remove_prefix(1);
  if (!row.empty() && row.back() == '|' && (row.size() < 2 || row[row.size() - 2] != '\\')) row.remove_suffix(1);
  int count = 0;
  size_t start = 0;
  bool inCode = false;
  for (size_t i = 0; i <= row.size(); i++) {
    if (i < row.size()) {
      if (row[i] == '\\') {
        i++;
        continue;
      }
      if (row[i] == '`') inCode = !inCode;
      if (row[i] != '|' || inCode) continue;
    }
    if (count < maxCells) cells[count++] = trim(row.substr(start, i - start));
    start = i + 1;
  }
  return count;
}

// List marker at the start of `rest`: "-", "*", "+" or "12." / "12)" followed by a space or end of line.
bool parseListMarker(const std::string_view rest, bool& ordered, int& number, size_t& contentStart) {
  if (rest.empty()) return false;
  if (rest[0] == '-' || rest[0] == '*' || rest[0] == '+') {
    if (rest.size() > 1 && !isSpace(rest[1])) return false;
    ordered = false;
    number = 0;
    contentStart = 1;
  } else {
    size_t i = 0;
    int value = 0;
    while (i < rest.size() && i < 9 && rest[i] >= '0' && rest[i] <= '9') {
      value = value * 10 + (rest[i] - '0');
      i++;
    }
    if (i == 0 || i >= rest.size() || (rest[i] != '.' && rest[i] != ')')) return false;
    if (i + 1 < rest.size() && !isSpace(rest[i + 1])) return false;
    ordered = true;
    number = value;
    contentStart = i + 1;
  }
  while (contentStart < rest.size() && isSpace(rest[contentStart])) contentStart++;
  return true;
}

void appendUtf8(std::string& o, const uint32_t cp) {
  if (cp < 0x80) {
    o.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// XML-escapes text and repairs it into something expat accepts: invalid UTF-8 becomes U+FFFD and
// control characters (illegal in XML 1.0) are dropped. One bad byte would otherwise fail the whole page build.
void escapeText(const std::string_view s, std::string& o) {
  size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      if (c == '&') {
        o += "&amp;";
      } else if (c == '<') {
        o += "&lt;";
      } else if (c == '>') {
        o += "&gt;";
      } else if (c == '\t') {
        o.push_back(' ');
      } else if (c >= 0x20 && c != 0x7F) {
        o.push_back(static_cast<char>(c));
      }
      i++;
      continue;
    }
    size_t len = 0;
    unsigned char lo = 0x80;
    unsigned char hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
      len = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
      len = 3;
      if (c == 0xE0) lo = 0xA0;
      if (c == 0xED) hi = 0x9F;  // no UTF-16 surrogates
    } else if (c >= 0xF0 && c <= 0xF4) {
      len = 4;
      if (c == 0xF0) lo = 0x90;
      if (c == 0xF4) hi = 0x8F;
    }
    bool valid = len > 0 && i + len <= s.size();
    for (size_t k = 1; valid && k < len; k++) {
      const auto cc = static_cast<unsigned char>(s[i + k]);
      valid = k == 1 ? (cc >= lo && cc <= hi) : (cc >= 0x80 && cc <= 0xBF);
    }
    if (valid) {
      o.append(s.data() + i, len);
      i += len;
    } else {
      o += "\xef\xbf\xbd";
      i++;
    }
  }
}

// Position of the backtick run of exactly `n` that closes a code span, or NPOS.
size_t findCodeCloser(const std::string_view s, size_t from, const size_t n) {
  while (from < s.size()) {
    if (s[from] == '`') {
      const size_t run = runLength(s, from, '`');
      if (run == n) return from;
      from += run;
    } else {
      from++;
    }
  }
  return NPOS;
}

// Finds the delimiter run that closes an emphasis opened with `n` x `c`. Returns the offset of the
// closing delimiters (the last `n` chars of the run), or NPOS. A run of 3 may close a run of 1 or 2,
// which is what lets "**bold *both***" nest.
size_t findEmphasisCloser(const std::string_view s, size_t from, const char c, const size_t n) {
  while (from < s.size()) {
    const char ch = s[from];
    if (ch == '\\') {
      from += 2;
      continue;
    }
    if (ch == '`') {
      const size_t run = runLength(s, from, '`');
      const size_t close = findCodeCloser(s, from + run, run);
      from = close == NPOS ? from + run : close + run;
      continue;
    }
    if (ch != c) {
      from++;
      continue;
    }
    const size_t run = runLength(s, from, c);
    const bool rightFlanking = from > 0 && !isSpace(s[from - 1]);
    const bool wordAfter = from + run < s.size() && isAlnum(s[from + run]);
    if (rightFlanking && (run == n || (run == 3 && n < 3)) && !(c == '_' && wordAfter)) {
      return from + run - n;
    }
    from += run;
  }
  return NPOS;
}

struct LinkSpan {
  std::string_view text;
  size_t end;
};

// [text](url), [text][ref] or [text][] starting at the '[' at `pos`.
bool parseLink(const std::string_view s, const size_t pos, LinkSpan& link) {
  int depth = 0;
  size_t i = pos;
  for (; i < s.size(); i++) {
    if (s[i] == '\\') {
      i++;
    } else if (s[i] == '[') {
      depth++;
    } else if (s[i] == ']' && --depth == 0) {
      break;
    }
  }
  if (i >= s.size() || i + 1 >= s.size()) return false;
  const size_t textEnd = i;
  const char opener = s[i + 1];
  if (opener != '(' && opener != '[') return false;
  const char closer = opener == '(' ? ')' : ']';
  int parens = 0;
  for (size_t j = i + 2; j < s.size(); j++) {
    if (s[j] == '\\') {
      j++;
    } else if (opener == '(' && s[j] == '(') {
      parens++;
    } else if (s[j] == closer) {
      if (parens-- > 0) continue;
      link.text = s.substr(pos + 1, textEnd - pos - 1);
      link.end = j + 1;
      return true;
    }
  }
  return false;
}

}  // namespace

void MarkdownToHtml::emit(const std::string_view s) {
  out.append(s.data(), s.size());
  flushOut(false);
}

void MarkdownToHtml::flushOut(const bool force) {
  if (!ok || out.empty() || (!force && out.size() < OUT_FLUSH_BYTES)) return;
  ok = sink.write(sink.ctx, out.data(), out.size());
  out.clear();
}

bool MarkdownToHtml::begin() {
  out.reserve(OUT_FLUSH_BYTES + 512);
  para.reserve(256);
  lists.reserve(MAX_LIST_DEPTH);
  emit("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<html><body>\n");
  return ok;
}

bool MarkdownToHtml::finish() {
  if (inFence || inIndentedCode) closeCode();
  closeTable();
  closeBlock();
  closeLists();
  emit("</body></html>\n");
  flushOut(true);
  return ok;
}

// Opens a block element's style attribute. The first block after a list gets a top gap so the list
// doesn't run straight into the next paragraph.
void MarkdownToHtml::blockStyle(const char* base) {
  emit(" style=\"");
  emit(base);
  if (gapBeforeNextBlock) {
    emit(";margin-top:0.5em");
    gapBeforeNextBlock = false;
  }
  emit("\"");
}

void MarkdownToHtml::appendToBlock(std::string_view text) {
  // A line ending in two spaces or a backslash is a hard line break.
  bool hardBreak = false;
  if (text.size() >= 2 && text.substr(text.size() - 2) == "  ") {
    hardBreak = true;
  } else if (!text.empty() && text.back() == '\\') {
    hardBreak = true;
    text.remove_suffix(1);
  }
  text = trim(text);
  if (!para.empty()) para.push_back(hardBreakPending ? '\n' : ' ');
  para.append(text.data(), text.size());
  hardBreakPending = hardBreak;
  if (paraLines++ == 0) paraFirstLine.assign(text.data(), text.size());

  if (para.size() > MAX_BLOCK_BYTES) {
    // Split runaway blocks; a list item continues without a second marker.
    const Block kind = block;
    closeBlock();
    block = kind;
    itemMarker.clear();
  }
}

void MarkdownToHtml::closeBlock() {
  if (block == Block::None) return;
  const std::string_view text = trim(para);
  switch (block) {
    case Block::Paragraph:
      if (!text.empty()) {
        emit("<p");
        blockStyle("text-indent:0;margin-bottom:0.6em");
        emit(">");
        renderInline(text, 0);
        emit("</p>\n");
      }
      break;
    case Block::Quote:
      if (!text.empty()) {
        emit("<blockquote");
        blockStyle("margin-left:1.2em;text-indent:0;margin-bottom:0.6em");
        emit("><i>");
        renderInline(text, 0);
        emit("</i></blockquote>\n");
      }
      break;
    case Block::ListItem:
      // Hanging indent: the marker sits in the negative first-line indent so wrapped lines align
      // with the item text.
      emit("<li");
      blockStyle(itemMarker.empty() ? "margin-left:1.2em;text-indent:0;margin-bottom:0.15em"
                                    : "margin-left:1.2em;text-indent:-1.2em;margin-bottom:0.15em");
      emit(">");
      if (!itemMarker.empty()) {
        emit(itemMarker);
        emit(NBSP);
      }
      renderInline(text, 0);
      emit("</li>\n");
      break;
    case Block::None:
      break;
  }
  block = Block::None;
  para.clear();
  paraLines = 0;
  paraFirstLine.clear();
  hardBreakPending = false;
}

// Each nesting level is a wrapper div: block margins accumulate through the parser's style stack,
// while a single element's margin-left is capped at 2em.
void MarkdownToHtml::pushList(const int indent, const bool ordered, const int number) {
  if (lists.size() >= MAX_LIST_DEPTH) {
    lists.back().indent = indent;
    return;
  }
  if (!lists.empty()) emit("<div style=\"margin-left:1.2em\">\n");
  lists.push_back({indent, ordered, number});
}

void MarkdownToHtml::popList() {
  if (lists.size() > 1) emit("</div>\n");
  lists.pop_back();
}

void MarkdownToHtml::closeLists() {
  if (lists.empty()) return;
  closeBlock();
  while (!lists.empty()) popList();
  gapBeforeNextBlock = true;
}

void MarkdownToHtml::openCode() {
  closeBlock();
  closeLists();
  emit("<div");
  blockStyle("margin-left:1em;text-indent:0;margin-bottom:0.6em");
  emit(">");
  codeHasLines = false;
}

// Code keeps its line breaks and leading indentation (as no-break spaces); there is no monospace font.
void MarkdownToHtml::emitCodeLine(const std::string_view line) {
  if (codeHasLines) emit("<br/>");
  codeHasLines = true;
  size_t start = 0;
  const int indent = measureIndent(line, start);
  for (int i = 0; i < indent; i++) emit(NBSP);
  const std::string_view rest = line.substr(start);
  if (rest.empty()) {
    if (indent == 0) emit(NBSP);
    return;
  }
  std::string escaped;
  escaped.reserve(rest.size() + 16);
  escapeText(rest, escaped);
  emit(escaped);
}

void MarkdownToHtml::closeCode() {
  if (!inFence && !inIndentedCode) return;
  if (!codeHasLines) emit(NBSP);
  emit("</div>\n");
  inFence = false;
  inIndentedCode = false;
}

void MarkdownToHtml::emitHeading(const int level, const std::string_view text) {
  char tag[4] = {'h', static_cast<char>('0' + level), '\0', '\0'};
  emit("<");
  emit(tag);
  if (level == 1) {
    blockStyle("text-align:center;text-indent:0;margin-top:0.4em;margin-bottom:0.2em");
  } else {
    blockStyle("text-align:left;text-indent:0;margin-top:0.5em;margin-bottom:0.3em");
  }
  emit(">");
  // Reader fonts have one size, so levels are told apart by decoration: h1 centred over a rule,
  // h2 underlined, h3 bold, h4-h6 bold italic.
  if (level == 2) emit("<u>");
  if (level >= 4) emit("<i>");
  renderInline(text, 0);
  if (level >= 4) emit("</i>");
  if (level == 2) emit("</u>");
  emit("</");
  emit(tag);
  emit(">\n");
  if (level == 1) emit("<hr/>\n");
}

void MarkdownToHtml::emitTableRow(const std::string_view line, const bool header) {
  std::string_view cells[MAX_TABLE_COLS];
  const int count = splitTableRow(line, cells, MAX_TABLE_COLS);
  if (header) tableCols = count;
  const char* cellTag = header ? "th" : "td";
  emit("<tr>");
  for (int i = 0; i < tableCols; i++) {
    emit("<");
    emit(cellTag);
    emit(">");
    if (i < count) renderInline(cells[i], 0);
    emit("</");
    emit(cellTag);
    emit(">");
  }
  emit("</tr>\n");
}

void MarkdownToHtml::closeTable() {
  if (!inTable) return;
  emit("</table>\n");
  inTable = false;
  gapBeforeNextBlock = true;
}

bool MarkdownToHtml::feedLine(std::string_view line) {
  if (!ok) return false;
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

  size_t contentStart = 0;
  const int indent = measureIndent(line, contentStart);
  const std::string_view rest = line.substr(contentStart);
  const bool blank = rest.empty();

  if (inFence) {
    if (indent < 4 && runLength(rest, 0, fenceChar) >= fenceLen && trim(rest.substr(runLength(rest, 0, fenceChar))).empty()) {
      closeCode();
      gapBeforeNextBlock = false;
    } else {
      emitCodeLine(line);
    }
    return ok;
  }

  if (inIndentedCode) {
    if (!blank && indent >= 4) {
      // Drop the 4-column code indent, keep anything deeper.
      size_t cut = 0;
      for (int cols = 0; cut < line.size() && cols < 4; cut++) {
        cols = line[cut] == '\t' ? 4 : cols + 1;
      }
      emitCodeLine(line.substr(cut));
      return ok;
    }
    closeCode();
  }

  if (inTable) {
    if (!blank && rest.find('|') != NPOS) {
      emitTableRow(rest, false);
      return ok;
    }
    closeTable();
  }

  if (blank) {
    closeBlock();
    blankBefore = true;
    return ok;
  }

  const bool wasBlank = blankBefore;
  blankBefore = false;

  if (indent < 4) {
    // Fenced code.
    if (rest[0] == '`' || rest[0] == '~') {
      const size_t run = runLength(rest, 0, rest[0]);
      if (run >= 3 && (rest[0] == '~' || rest.substr(run).find('`') == NPOS)) {
        openCode();
        inFence = true;
        fenceChar = rest[0];
        fenceLen = run;
        return ok;
      }
    }

    // ATX heading.
    if (rest[0] == '#') {
      const size_t hashes = runLength(rest, 0, '#');
      if (hashes <= 6 && (hashes == rest.size() || isSpace(rest[hashes]))) {
        std::string_view text = trim(rest.substr(hashes));
        // Optional closing hashes ("## Title ##").
        size_t end = text.size();
        while (end > 0 && text[end - 1] == '#') end--;
        if (end == 0 || isSpace(text[end - 1])) text = trim(text.substr(0, end));
        closeBlock();
        closeLists();
        emitHeading(static_cast<int>(hashes), text);
        return ok;
      }
    }

    if (block == Block::Paragraph) {
      // Setext heading underline.
      const std::string_view underline = trim(rest);
      if (allChar(underline, '=') || allChar(underline, '-')) {
        const std::string text = para;
        const int level = underline[0] == '=' ? 1 : 2;
        para.clear();
        block = Block::None;
        paraLines = 0;
        emitHeading(level, text);
        return ok;
      }
      // Pipe table: a one-line paragraph with pipes followed by a delimiter row.
      if (paraLines == 1 && paraFirstLine.find('|') != NPOS && isTableSeparator(rest)) {
        const std::string header = paraFirstLine;
        para.clear();
        block = Block::None;
        paraLines = 0;
        closeLists();
        emit("<table>\n");
        inTable = true;
        emitTableRow(header, true);
        return ok;
      }
    }

    if (isHorizontalRule(rest)) {
      closeBlock();
      closeLists();
      emit("<hr/>\n");
      return ok;
    }

    if (rest[0] == '>') {
      std::string_view quoted = rest.substr(1);
      if (!quoted.empty() && isSpace(quoted[0])) quoted.remove_prefix(1);
      if (trim(quoted).empty()) {
        closeBlock();
      } else {
        if (block != Block::Quote) {
          closeBlock();
          closeLists();
          block = Block::Quote;
        }
        appendToBlock(quoted);
      }
      return ok;
    }
  }

  bool ordered = false;
  int number = 0;
  size_t markerEnd = 0;
  if ((indent < 4 || !lists.empty()) && parseListMarker(rest, ordered, number, markerEnd)) {
    closeBlock();
    if (lists.empty()) {
      pushList(indent, ordered, number);
    } else {
      while (lists.size() > 1 && indent < lists.back().indent) popList();
      if (indent >= lists.back().indent + 2) {
        pushList(indent, ordered, number);
      } else if (lists.back().ordered != ordered) {
        lists.back().ordered = ordered;
        lists.back().nextNumber = number;
        gapBeforeNextBlock = true;
      }
    }
    ListLevel& level = lists.back();
    std::string_view content = rest.substr(markerEnd);
    if (ordered) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d.", level.nextNumber++);
      itemMarker = buf;
    } else {
      itemMarker = BULLETS[(lists.size() - 1) % std::size(BULLETS)];
    }
    // Task list item: the box replaces the bullet (the reader fonts have no ballot-box glyph).
    if (content.size() >= 3 && content[0] == '[' && content[2] == ']' &&
        (content[1] == ' ' || content[1] == 'x' || content[1] == 'X') && (content.size() == 3 || isSpace(content[3]))) {
      itemMarker = content[1] == ' ' ? "[&#160;&#160;]" : "[x]";
      content = trim(content.substr(3));
    }
    block = Block::ListItem;
    appendToBlock(content);
    return ok;
  }

  if (!lists.empty()) {
    if (indent >= lists.back().indent + 2) {
      if (block == Block::ListItem && !wasBlank) {
        appendToBlock(rest);
      } else {
        // A further paragraph inside the item: same indent, no marker.
        closeBlock();
        block = Block::ListItem;
        itemMarker.clear();
        appendToBlock(rest);
      }
      return ok;
    }
    if (wasBlank || block != Block::ListItem) closeLists();
  }

  if (indent >= 4 && block == Block::None && wasBlank) {
    openCode();
    inIndentedCode = true;
    emitCodeLine(line.substr(contentStart));
    return ok;
  }

  // Lazy continuation of an open paragraph, quote or list item.
  if (block != Block::None) {
    appendToBlock(rest);
    return ok;
  }

  block = Block::Paragraph;
  appendToBlock(rest);
  return ok;
}

void MarkdownToHtml::renderInline(const std::string_view s, const int depth) {
  std::string o;
  o.reserve(s.size() + 32);
  if (depth > MAX_INLINE_DEPTH) {
    escapeText(s, o);
    emit(o);
    return;
  }

  size_t i = 0;
  size_t plainStart = 0;
  auto flushPlain = [&](const size_t upTo) {
    if (upTo > plainStart) escapeText(s.substr(plainStart, upTo - plainStart), o);
  };
  // Nested spans are rendered straight into `out`, so pending text must go first.
  auto flushPending = [&]() {
    emit(o);
    o.clear();
  };

  while (i < s.size()) {
    const char c = s[i];

    if (c == '\\' && i + 1 < s.size() && isAsciiPunct(s[i + 1])) {
      flushPlain(i);
      escapeText(s.substr(i + 1, 1), o);
      i += 2;
      plainStart = i;
      continue;
    }

    if (c == '\n') {
      flushPlain(i);
      o += "<br/>";
      plainStart = ++i;
      continue;
    }

    if (c == '`') {
      const size_t run = runLength(s, i, '`');
      const size_t close = findCodeCloser(s, i + run, run);
      flushPlain(i);
      if (close == NPOS) {
        escapeText(s.substr(i, run), o);
        i += run;
      } else {
        std::string_view code = s.substr(i + run, close - i - run);
        if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ') code = code.substr(1, code.size() - 2);
        escapeText(code, o);
        i = close + run;
      }
      plainStart = i;
      continue;
    }

    if (c == '[' || (c == '!' && i + 1 < s.size() && s[i + 1] == '[')) {
      LinkSpan link;
      const bool image = c == '!';
      if (parseLink(s, image ? i + 1 : i, link)) {
        flushPlain(i);
        flushPending();
        // Links can't be followed on the device, so they are underlined text; images show their alt text.
        emit(image ? "<i>[" : "<u>");
        renderInline(link.text, depth + 1);
        emit(image ? "]</i>" : "</u>");
        i = link.end;
        plainStart = i;
        continue;
      }
    }

    if (c == '<') {
      const size_t close = s.find('>', i + 1);
      if (close != NPOS) {
        const std::string_view inner = s.substr(i + 1, close - i - 1);
        const bool url = inner.substr(0, 7) == "http://" || inner.substr(0, 8) == "https://" ||
                         inner.substr(0, 7) == "mailto:";
        const bool email = !url && inner.find('@') != NPOS && inner.find(' ') == NPOS;
        if ((url || email) && inner.find(' ') == NPOS) {
          flushPlain(i);
          o += "<u>";
          escapeText(inner, o);
          o += "</u>";
          i = close + 1;
          plainStart = i;
          continue;
        }
        if (inner == "br" || inner == "br/" || inner == "br /") {
          flushPlain(i);
          o += "<br/>";
          i = close + 1;
          plainStart = i;
          continue;
        }
      }
    }

    if (c == '&') {
      const size_t semi = s.find(';', i + 1);
      if (semi != NPOS && semi - i <= 12) {
        const std::string_view ent = s.substr(i, semi - i + 1);
        bool handled = false;
        if (ent.size() > 3 && ent[1] == '#') {
          uint32_t cp = 0;
          const bool hex = ent[2] == 'x' || ent[2] == 'X';
          bool digits = true;
          for (size_t k = hex ? 3 : 2; k + 1 < ent.size(); k++) {
            const char d = ent[k];
            int v = -1;
            if (d >= '0' && d <= '9') v = d - '0';
            if (hex && d >= 'a' && d <= 'f') v = d - 'a' + 10;
            if (hex && d >= 'A' && d <= 'F') v = d - 'A' + 10;
            if (v < 0) digits = false;
            cp = cp * (hex ? 16 : 10) + (v < 0 ? 0 : v);
          }
          // Only code points XML can carry.
          if (digits && (cp == 0x9 || cp == 0xA || (cp >= 0x20 && cp < 0xD800) || (cp > 0xDFFF && cp < 0xFFFE) ||
                         (cp >= 0x10000 && cp <= 0x10FFFF))) {
            flushPlain(i);
            std::string utf8;
            appendUtf8(utf8, cp);
            escapeText(utf8, o);
            handled = true;
          }
        } else if (const char* value = lookupHtmlEntity(ent.data(), ent.size())) {
          flushPlain(i);
          escapeText(value, o);
          handled = true;
        }
        if (handled) {
          i = semi + 1;
          plainStart = i;
          continue;
        }
      }
    }

    if (c == '*' || c == '_' || c == '~') {
      const size_t run = runLength(s, i, c);
      const bool leftFlanking = i + run < s.size() && !isSpace(s[i + run]);
      const bool wordBefore = i > 0 && isAlnum(s[i - 1]);
      const bool usable = c == '~' ? run == 2 : run <= 3;
      if (leftFlanking && usable && !(c == '_' && wordBefore)) {
        const size_t close = findEmphasisCloser(s, i + run, c, run);
        if (close != NPOS && close > i + run) {
          flushPlain(i);
          flushPending();
          const char* openTag = c == '~' ? "<s>" : run == 1 ? "<i>" : run == 2 ? "<b>" : "<b><i>";
          const char* closeTag = c == '~' ? "</s>" : run == 1 ? "</i>" : run == 2 ? "</b>" : "</i></b>";
          emit(openTag);
          renderInline(s.substr(i + run, close - i - run), depth + 1);
          emit(closeTag);
          i = close + run;
          plainStart = i;
          continue;
        }
      }
      i += run;
      continue;
    }

    i++;
  }
  flushPlain(s.size());
  emit(o);
}
