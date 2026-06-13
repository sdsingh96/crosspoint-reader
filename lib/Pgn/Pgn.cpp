#include "Pgn.h"
#include <Logging.h>

Pgn::Pgn(std::string path) : filepath(std::move(path)) {}

static std::string extractTag(const std::string& line, const std::string& tag) {
    std::string prefix = "[" + tag + " \"";
    size_t start = line.find(prefix);
    if (start != std::string::npos) {
        start += prefix.length();
        size_t end = line.find("\"", start);
        if (end != std::string::npos) {
            return line.substr(start, end - start);
        }
    }
    return "";
}

bool Pgn::load() {
  if (loaded) return true;

  auto file = Storage.open(filepath.c_str());
  if (!file) {
    LOG_ERR("PGN", "Failed to open PGN file: %s", filepath.c_str());
    return false;
  }

  PgnGame currentGame;
  bool inMoveText = false;
  uint32_t currentOffset = 0;

  while (file.available()) {
      currentOffset = file.position();
      std::string line;
      char ch;
      while (file.read(&ch, 1) == 1) {
          if (ch == '\n') break;
          line += ch;
      }
      if (!line.empty() && line.back() == '\r') line.pop_back();

      if (line.empty()) {
          continue;
      }

      if (line[0] == '[') {
          if (inMoveText) {
              currentGame.moveTextLength = currentOffset - currentGame.moveTextOffset;
              if (games.size() < 200) {
                  games.push_back(currentGame);
              }
              currentGame = PgnGame();
              inMoveText = false;
          }
          std::string w = extractTag(line, "White");
          if (!w.empty()) currentGame.white = w;
          std::string b = extractTag(line, "Black");
          if (!b.empty()) currentGame.black = b;
          std::string e = extractTag(line, "Event");
          if (!e.empty()) currentGame.event = e;
      } else {
          if (!inMoveText) {
              inMoveText = true;
              currentGame.moveTextOffset = currentOffset;
          }
      }
  }
  
  if (inMoveText) {
      currentGame.moveTextLength = file.size() - currentGame.moveTextOffset;
      if (games.size() < 200) {
          games.push_back(currentGame);
      }
  }

  file.close();

  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');
  if (lastSlash != std::string::npos && lastDot != std::string::npos && lastDot > lastSlash) {
    title = filepath.substr(lastSlash + 1, lastDot - lastSlash - 1);
  } else {
    title = filepath;
  }

  loaded = true;
  return true;
}

std::string Pgn::getMoveText(const PgnGame& game) const {
    auto file = Storage.open(filepath.c_str());
    if (!file) return "";
    
    file.seek(game.moveTextOffset);
    std::string text;
    text.resize(game.moveTextLength);
    file.read(reinterpret_cast<uint8_t*>(&text[0]), game.moveTextLength);
    file.close();
    
    return text;
}
