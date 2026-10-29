#include "InspirationsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int SIDE_PADDING = 36;
constexpr int MAX_LINES_PER_SEGMENT = 8;
// Longest single '\n'-separated segment (bytes). Every built-in entry fits.
constexpr size_t MAX_SEGMENT_BYTES = 191;
}  // namespace

void InspirationsActivity::onEnter() {
  Activity::onEnter();

  // Fisher-Yates shuffle so quotes and haiku come up mixed; random() is backed by the hardware RNG.
  for (size_t i = 0; i < INSPIRATION_COUNT; i++) {
    order[i] = static_cast<uint8_t>(i);
  }
  for (size_t i = INSPIRATION_COUNT - 1; i > 0; i--) {
    std::swap(order[i], order[random(static_cast<long>(i + 1))]);
  }
  position = 0;

  requestUpdate();
}

void InspirationsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  buttonNavigator.onNext([this] {
    position = ButtonNavigator::nextIndex(position, static_cast<int>(INSPIRATION_COUNT));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    position = ButtonNavigator::previousIndex(position, static_cast<int>(INSPIRATION_COUNT));
    requestUpdate();
  });
}

void InspirationsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.sideButtonHintsWidth : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = renderer.getScreenWidth() - hintGutterWidth;
  const int textWidth = contentWidth - 2 * SIDE_PADDING;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_INSPIRATIONS));

  const Inspiration& entry = INSPIRATIONS[order[position]];
  const int fontId = entry.haiku ? YOUNGSERIF_18_FONT_ID : YOUNGSERIF_16_FONT_ID;
  const auto style = entry.haiku ? EpdFontFamily::ITALIC : EpdFontFamily::REGULAR;

  // Each '\n' segment is wrapped on its own, so haiku keep their line breaks.
  std::vector<std::string> lines;
  lines.reserve(12);
  char segment[MAX_SEGMENT_BYTES + 1];
  const char* cursor = entry.text;
  while (true) {
    const char* newline = strchr(cursor, '\n');
    const size_t len = newline ? static_cast<size_t>(newline - cursor) : strlen(cursor);
    const size_t copyLen = std::min(len, MAX_SEGMENT_BYTES);
    memcpy(segment, cursor, copyLen);
    segment[copyLen] = '\0';
    for (auto& line : renderer.wrappedText(fontId, segment, textWidth, MAX_LINES_PER_SEGMENT, style)) {
      lines.push_back(std::move(line));
    }
    if (!newline) break;
    cursor = newline + 1;
  }

  char attribution[96];
  snprintf(attribution, sizeof(attribution), "— %s", entry.author);

  // Centre the text block plus attribution between the header and the button hints.
  const int lineHeight = renderer.getLineHeight(fontId);
  const int authorFontId = YOUNGSERIF_12_FONT_ID;
  const int authorGap = lineHeight;
  const int blockHeight =
      static_cast<int>(lines.size()) * lineHeight + authorGap + renderer.getLineHeight(authorFontId);
  const int bodyTop = metrics.topPadding + metrics.headerHeight;
  const int bodyHeight = renderer.getScreenHeight() - metrics.buttonHintsHeight - bodyTop;
  int y = bodyTop + std::max(0, (bodyHeight - blockHeight) / 2);

  for (const auto& line : lines) {
    const int w = renderer.getTextWidth(fontId, line.c_str(), style);
    renderer.drawText(fontId, contentX + (contentWidth - w) / 2, y, line.c_str(), true, style);
    y += lineHeight;
  }
  y += authorGap;
  const int authorWidth = renderer.getTextWidth(authorFontId, attribution);
  renderer.drawText(authorFontId, contentX + (contentWidth - authorWidth) / 2, y, attribution);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
