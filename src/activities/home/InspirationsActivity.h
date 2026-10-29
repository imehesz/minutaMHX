#pragma once

#include <cstdint>

#include "InspirationsData.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// One inspirational quote or haiku per screen. Opens on a random entry;
// Prev/Next walk a shuffled order that wraps around.
class InspirationsActivity final : public Activity {
 public:
  explicit InspirationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Inspirations", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static_assert(INSPIRATION_COUNT <= 256, "order[] stores indices as uint8_t");

  ButtonNavigator buttonNavigator;
  uint8_t order[INSPIRATION_COUNT]{};
  int position = 0;
};
