#pragma once

#include <memory>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "Pgn.h"

class PgnListActivity final : public Activity {
  std::unique_ptr<Pgn> pgn;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void handleSelection();

 public:
  explicit PgnListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Pgn> pgn);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
