#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "Pgn.h"
#include "thc.h"

class PgnReaderActivity final : public Activity {
  std::string gameTitle;
  std::string gameMoveText;
  std::vector<std::string> moveStrings;
  std::vector<thc::Move> moves;
  int currentMoveIndex = 0;
  thc::ChessRules cr;
  int movesUntilFullRefresh = 0;



  void parsePgn();
  void rebuildBoard();
  void renderBoard() const;

 public:
  explicit PgnReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, std::string moveText);

  void onEnter() override;
  void loop() override;
  bool isReaderActivity() const override { return true; }
};
