#pragma once

#include <HalStorage.h>
#include <string>
#include <vector>

struct PgnGame {
  std::string white;
  std::string black;
  std::string event;
  uint32_t moveTextOffset = 0;
  uint32_t moveTextLength = 0;
};

class Pgn {
  std::string filepath;
  std::string title;
  std::vector<PgnGame> games;
  bool loaded = false;

 public:
  explicit Pgn(std::string path);

  bool load();
  std::string getMoveText(const PgnGame& game) const;
  
  [[nodiscard]] const std::string& getPath() const { return filepath; }
  [[nodiscard]] const std::string& getTitle() const { return title; }
  [[nodiscard]] const std::vector<PgnGame>& getGames() const { return games; }
};
