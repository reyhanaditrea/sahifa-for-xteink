#pragma once
#include "activities/Activity.h"

class BootActivity final : public Activity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Boot", renderer, mappedInput) {}
  void onEnter() override;

  // Fills the boot screen's progress bar to step/steps and pushes it to the panel.
  // Called from setup() between boot stages, while the boot screen is still the
  // framebuffer's content.
  static void showProgress(const GfxRenderer& renderer, int step, int steps);
};
