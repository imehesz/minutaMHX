#include "TxtEditorActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr char TEMP_SUFFIX[] = ".mhxsave";

// Largest prefix of s no longer than maxBytes that ends on a code point boundary.
size_t utf8PrefixLength(const std::string& s, const size_t maxBytes) {
  if (s.size() <= maxBytes) return s.size();
  size_t len = maxBytes;
  while (len > 0 && (static_cast<unsigned char>(s[len]) & 0xC0) == 0x80) len--;
  return len;
}
}  // namespace

TxtEditorActivity::TxtEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : UiListActivity("TxtEditor", renderer, mappedInput), path(std::move(path)) {
  const size_t slash = this->path.find_last_of('/');
  displayName = slash == std::string::npos ? this->path : this->path.substr(slash + 1);
}

void TxtEditorActivity::onEnter() {
  if (!loaded) {
    loaded = true;
    loadFile();
  }
  UiListActivity::onEnter();
}

bool TxtEditorActivity::loadFile() {
  HalFile file;
  if (!Storage.openFileForRead("TXE", path, file)) {
    errorText = tr(STR_TXT_EDITOR_OPEN_FAILED);
    return false;
  }
  const size_t size = file.fileSize();
  if (size > MAX_EDIT_BYTES) {
    LOG_ERR("TXE", "File too large to edit: %u bytes", static_cast<unsigned>(size));
    errorText = tr(STR_TXT_EDITOR_TOO_LARGE);
    return false;
  }
  // Splitting holds the text twice (raw + lines); leave headroom for the
  // keyboard and renderer on top of that.
  if (ESP.getFreeHeap() < size * 3 + 32 * 1024) {
    LOG_ERR("TXE", "Low heap for %u byte file: %u free", static_cast<unsigned>(size),
            static_cast<unsigned>(ESP.getFreeHeap()));
    errorText = tr(STR_TXT_EDITOR_LOW_MEMORY);
    return false;
  }

  std::string raw;
  raw.resize(size);
  if (size > 0 && file.read(raw.data(), size) != static_cast<int>(size)) {
    LOG_ERR("TXE", "Short read: %s", path.c_str());
    errorText = tr(STR_TXT_EDITOR_OPEN_FAILED);
    return false;
  }

  crlf = raw.find("\r\n") != std::string::npos;
  trailingNewline = !raw.empty() && raw.back() == '\n';
  lines.reserve(std::count(raw.begin(), raw.end(), '\n') + 1);
  size_t start = 0;
  while (true) {
    const size_t newline = raw.find('\n', start);
    const size_t end = newline == std::string::npos ? raw.size() : newline;
    size_t len = end - start;
    if (len > 0 && raw[start + len - 1] == '\r') len--;
    lines.emplace_back(raw, start, len);
    if (newline == std::string::npos) break;
    start = newline + 1;
  }
  // "a\nb\n" is two lines, not two plus an empty third.
  if (trailingNewline && lines.size() > 1 && lines.back().empty()) lines.pop_back();
  return true;
}

bool TxtEditorActivity::saveFile() {
  const std::string tempPath = path + TEMP_SUFFIX;
  if (Storage.exists(tempPath.c_str())) Storage.remove(tempPath.c_str());

  {
    HalFile file;
    if (!Storage.openFileForWrite("TXE", tempPath, file)) {
      LOG_ERR("TXE", "Cannot create %s", tempPath.c_str());
      return false;
    }
    const char* eol = crlf ? "\r\n" : "\n";
    const size_t eolLen = crlf ? 2 : 1;
    bool ok = true;
    for (size_t i = 0; ok && i < lines.size(); i++) {
      const std::string& line = lines[i];
      ok = file.write(line.data(), line.size()) == line.size();
      if (ok && (i + 1 < lines.size() || trailingNewline)) ok = file.write(eol, eolLen) == eolLen;
    }
    if (!ok) {
      LOG_ERR("TXE", "Write failed: %s", tempPath.c_str());
      file.close();
      Storage.remove(tempPath.c_str());
      return false;
    }
  }

  // The new text is complete on the card before the original goes away.
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("TXE", "Cannot replace %s; new text kept in %s", path.c_str(), tempPath.c_str());
    return false;
  }
  if (!Storage.rename(tempPath.c_str(), path.c_str())) {
    LOG_ERR("TXE", "Rename failed; new text kept in %s", tempPath.c_str());
    return false;
  }
  // Page index and progress describe the old text.
  clearBookCache(path);
  dirty = false;
  return true;
}

void TxtEditorActivity::markDirty() {
  dirty = true;
  statusText = nullptr;
  requestUpdate();
}

void TxtEditorActivity::refreshWindow(const int start) {
  const int total = listCount();
  int clamped = std::min(start, total - LINE_WINDOW);
  if (clamped < 0) clamped = 0;
  if (clamped == windowStart) return;

  windowCount = std::min(LINE_WINDOW, total - clamped);
  for (int i = 0; i < windowCount; i++) {
    const std::string& line = lines[clamped + i];
    char number[12];
    snprintf(number, sizeof(number), "%d  ", clamped + i + 1);
    windowLabels[i] = number;
    if (line.empty()) {
      windowLabels[i] += tr(STR_TXT_EDITOR_EMPTY_LINE);
    } else {
      windowLabels[i].append(line, 0, utf8PrefixLength(line, LABEL_MAX_BYTES));
    }
    fui::ListItem item;
    item.label = windowLabels[i].c_str();
    item.actionValue = static_cast<int16_t>(clamped + i);
    item.italic = line.empty();
    windowItems[i] = item;
  }
  windowStart = clamped;
}

void TxtEditorActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 3), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  if (errorText) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, errorText, true);
    return;
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  props.rowGap = 0;
  syncListViewport(screen, props);
  refreshWindow(nav.top);
  props.items = windowItems;
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  props.itemsWindowCount = static_cast<uint16_t>(windowCount);
  props.labelText = screen.theme().bodyText;
  props.labelText.align = fui::TextAlign::Left;
  screen.list(props);
}

void TxtEditorActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const char* subtitle = statusText ? statusText
                         : dirty    ? tr(STR_TXT_EDITOR_UNSAVED)
                         : errorText ? nullptr
                                     : tr(STR_TXT_EDITOR_HOLD_HINT);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, renderer.getScreenWidth(), metrics.headerHeight},
                 displayName.c_str(), subtitle);
}

void TxtEditorActivity::drawFooter() {
  const auto labels = errorText ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                                : mappedInput.mapLabels(tr(STR_BACK), tr(STR_EDIT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void TxtEditorActivity::render(RenderLock&& lock) {
  if (!popup.isActive()) {
    UiListActivity::render(std::move(lock));
    return;
  }
  renderer.clearScreen();
  drawChrome();
  renderUi();
  popup.processRender(renderer, mappedInput);
}

void TxtEditorActivity::activateIndex(const int index) {
  if (index >= 0 && index < listCount()) editLine(index, false);
}

void TxtEditorActivity::editLine(const int index, const bool removeIfCancelled) {
  app.clearTapFlash();
  char title[32];
  snprintf(title, sizeof(title), "%s %d", tr(STR_TXT_EDITOR_LINE), index + 1);
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, title, lines[index], 0, InputType::Text),
      [this, index, removeIfCancelled](const ActivityResult& result) {
        if (index >= listCount()) return;
        if (result.isCancelled) {
          // Backing out of a just-inserted line leaves the file as it was.
          if (removeIfCancelled && lines[index].empty()) deleteLine(index);
          return;
        }
        const std::string& text = std::get<KeyboardResult>(result.data).text;
        if (text == lines[index]) return;
        {
          RenderLock lock(*this);
          lines[index] = text;
          invalidateWindow();
        }
        markDirty();
      });
}

void TxtEditorActivity::insertLine(const int index) {
  {
    RenderLock lock(*this);
    lines.insert(lines.begin() + index, std::string());
    invalidateWindow();
  }
  moveSelectionTo(index);
  markDirty();
  editLine(index, true);
}

void TxtEditorActivity::deleteLine(const int index) {
  {
    RenderLock lock(*this);
    if (lines.size() == 1) {
      lines[0].clear();
    } else {
      lines.erase(lines.begin() + index);
    }
    invalidateWindow();
  }
  moveSelectionTo(std::min(index, listCount() - 1));
  markDirty();
}

void TxtEditorActivity::openLineActions() {
  const int index = nav.selected;
  if (index < 0 || index >= listCount()) return;

  char title[32];
  snprintf(title, sizeof(title), "%s %d", tr(STR_TXT_EDITOR_LINE), index + 1);
  popupTitle = title;
  const char* options[] = {tr(STR_TXT_EDITOR_INSERT_BELOW), tr(STR_TXT_EDITOR_INSERT_ABOVE),
                           tr(STR_TXT_EDITOR_DELETE_LINE), tr(STR_TXT_EDITOR_SAVE)};
  popup.show(popupTitle.c_str(), options, 4, 0, [this, index](const int choice) {
    switch (choice) {
      case 0:
        insertLine(index + 1);
        break;
      case 1:
        insertLine(index);
        break;
      case 2:
        deleteLine(index);
        break;
      default:
        statusText = saveFile() ? tr(STR_SAVED) : tr(STR_TXT_EDITOR_SAVE_FAILED);
        requestUpdate();
        break;
    }
  });
  requestUpdate();
}

void TxtEditorActivity::onBackButton() {
  if (!dirty) {
    finish();
    return;
  }
  popupTitle = tr(STR_TXT_EDITOR_UNSAVED);
  const char* options[] = {tr(STR_TXT_EDITOR_SAVE_EXIT), tr(STR_TXT_EDITOR_DISCARD), tr(STR_TXT_EDITOR_KEEP_EDITING)};
  popup.show(popupTitle.c_str(), options, 3, 0, [this](const int choice) {
    if (choice == 0) {
      if (saveFile()) {
        finish();
      } else {
        statusText = tr(STR_TXT_EDITOR_SAVE_FAILED);
      }
    } else if (choice == 1) {
      finish();
    }
  });
  requestUpdate();
}

bool TxtEditorActivity::handleCustomInput() {
  if (swallowConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      swallowConfirmRelease = false;
      return true;
    }
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) swallowConfirmRelease = false;
  }
  if (popup.isActive()) {
    popup.handleInput(mappedInput, [this] { requestUpdate(); });
    return true;
  }
  return false;
}

bool TxtEditorActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  if (errorText) return true;

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }
  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    confirmLongHandled = true;
    swallowConfirmRelease = true;
    openLineActions();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool shortPress = confirmHeld && !confirmLongHandled;
    confirmHeld = false;
    confirmLongHandled = false;
    if (shortPress) activateIndex(nav.selected);
    return true;
  }
  return false;
}
