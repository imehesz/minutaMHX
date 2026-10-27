#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Line editor for .txt files. Rows are the file's lines; Confirm edits the
// selected line in the on-screen keyboard (which cannot scroll a whole file),
// and holding Confirm opens line actions (insert, delete, save). Saving writes
// "<path>.mhxsave" first and swaps it in, so a failed write never loses text.
class TxtEditorActivity final : public UiListActivity {
 public:
  explicit TxtEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path);

  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  // The whole file lives in RAM while editing; 32 KB of notes is plenty for
  // on-the-road edits and keeps the editor clear of the C3's heap ceiling.
  static constexpr size_t MAX_EDIT_BYTES = 32 * 1024;
  // Rows show at most this many bytes of a line; the list ellipsizes the rest.
  static constexpr size_t LABEL_MAX_BYTES = 96;
  static constexpr int LINE_WINDOW = 24;
  static constexpr unsigned long LONG_PRESS_MS = 700;

  std::string path;
  std::string displayName;
  std::vector<std::string> lines;
  bool crlf = false;
  bool trailingNewline = false;
  bool loaded = false;
  bool dirty = false;
  const char* errorText = nullptr;
  const char* statusText = nullptr;

  // Rows are materialized only around the viewport (see fui::ListProps).
  std::string windowLabels[LINE_WINDOW];
  freeink::ui::ListItem windowItems[LINE_WINDOW];
  int windowStart = -1;
  int windowCount = 0;

  OptionPopup popup;
  std::string popupTitle;
  bool confirmHeld = false;
  bool confirmLongHandled = false;
  // The Confirm hold that opens the line actions ends in a release the popup
  // would otherwise take as a selection.
  bool swallowConfirmRelease = false;

  bool loadFile();
  bool saveFile();
  void refreshWindow(int start);
  void invalidateWindow() { windowStart = -1; }
  void editLine(int index, bool removeIfCancelled);
  void insertLine(int index);
  void deleteLine(int index);
  void openLineActions();
  void markDirty();

  int listCount() const override { return static_cast<int>(lines.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void onBackButton() override;
  void drawChrome() override;
  void drawFooter() override;
};
