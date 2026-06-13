#include "PgnReaderActivity.h"
#include <cctype>
#include <sstream>
#include <Logging.h>
#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "ReaderUtils.h"

PgnReaderActivity::PgnReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, std::string moveText)
    : Activity("PgnReader", renderer, mappedInput), gameTitle(std::move(title)), gameMoveText(std::move(moveText)) {}

void PgnReaderActivity::onEnter() {
  Activity::onEnter();
  parsePgn();
  currentMoveIndex = 0;
  movesUntilFullRefresh = 0;
  rebuildBoard();
  renderBoard();
  ReaderUtils::displayWithRefreshCycle(renderer, movesUntilFullRefresh);
}

void PgnReaderActivity::parsePgn() {
  const std::string& content = gameMoveText;
  int i = 0;
  int n = content.length();
  
  thc::ChessRules parserCr;
  
  while (i < n) {
    if (content[i] == '[') {
      while (i < n && content[i] != ']') i++;
      i++;
      continue;
    }
    if (content[i] == '{') {
      while (i < n && content[i] != '}') i++;
      i++;
      continue;
    }
    if (content[i] == ';') {
      while (i < n && content[i] != '\n') i++;
      i++;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(content[i]))) {
      i++;
      continue;
    }
    
    std::string token;
    while (i < n && !std::isspace(static_cast<unsigned char>(content[i])) && content[i] != '[' && content[i] != '{' && content[i] != ';') {
      token += content[i];
      i++;
    }
    if (token.empty()) continue;
    
    if (token.back() == '.') continue;
    
    size_t start = 0;
    while (start < token.length() && (std::isdigit(static_cast<unsigned char>(token[start])) || token[start] == '.')) {
      start++;
    }
    if (start > 0) {
      token = token.substr(start);
    }
    if (token.empty()) continue;
    
    if (token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*") continue;
    
    thc::Move m;
    if (m.NaturalIn(&parserCr, token.c_str())) {
      moves.push_back(m);
      moveStrings.push_back(token);
      parserCr.PlayMove(m);
    }
  }
}

void PgnReaderActivity::rebuildBoard() {
  cr = thc::ChessRules(); // Completely reset the board state
  for (int i = 0; i < currentMoveIndex && i < moves.size(); i++) {
    cr.PlayMove(moves[i]);
  }
}

void PgnReaderActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Right) || mappedInput.wasPressed(MappedInputManager::Button::Down) || mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    if (currentMoveIndex < moves.size()) {
      currentMoveIndex++;
      rebuildBoard();
      renderBoard();
      ReaderUtils::displayWithRefreshCycle(renderer, movesUntilFullRefresh);
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left) || mappedInput.wasPressed(MappedInputManager::Button::Up) || mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    if (currentMoveIndex > 0) {
      currentMoveIndex--;
      rebuildBoard();
      renderBoard();
      ReaderUtils::displayWithRefreshCycle(renderer, movesUntilFullRefresh);
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

namespace {
const char* PIECE_PAWN[40] = {
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
".................######.................",
"...............##OOOOOO##...............",
"..............#OOOOOOOOOO#..............",
"..............#OOOOOOOOOO#..............",
"..............#OOOOOOOOOO#..............",
"...............##OOOOOO##...............",
".................######.................",
"................########................",
"...............#OOOOOOOO#...............",
"...............##########...............",
"................#OOOOOO#................",
"................#OOOOOO#................",
"...............#OOOOOOOO#...............",
"...............#OOOOOOOO#...............",
"..............#OOOOOOOOOO#..............",
"..............#OOOOOOOOOO#..............",
".............#OOOOOOOOOOOO#.............",
".............#OOOOOOOOOOOO#.............",
"............#OOOOOOOOOOOOOO#............",
"............#OOOOOOOOOOOOOO#............",
"...........#OOOOOOOOOOOOOOOO#...........",
"...........#OOOOOOOOOOOOOOOO#...........",
"...........##################...........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
".........#OOOOOOOOOOOOOOOOOOOO#.........",
".........######################.........",
"........................................"
};

const char* PIECE_ROOK[40] = {
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
".......####......####......####.........",
"......#OOOO#....#OOOO#....#OOOO#........",
"......#OOOOO####OOOOOO####OOOOO#........",
"......#OOOOOOOOOOOOOOOOOOOOOOOO#........",
"......#OOOOOOOOOOOOOOOOOOOOOOOO#........",
"......##########################........",
".......#OOOOOOOOOOOOOOOOOOOOOO#.........",
".......########################.........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
"........#OOOOOOOOOOOOOOOOOOOO#..........",
".......##OOOOOOOOOOOOOOOOOOOO##.........",
"......#OOOOOOOOOOOOOOOOOOOOOOOO#........",
"......#OOOOOOOOOOOOOOOOOOOOOOOO#........",
"......##########################........",
".....#OOOOOOOOOOOOOOOOOOOOOOOOOO#.......",
".....#OOOOOOOOOOOOOOOOOOOOOOOOOO#.......",
"....#OOOOOOOOOOOOOOOOOOOOOOOOOOOO#......",
"....#OOOOOOOOOOOOOOOOOOOOOOOOOOOO#......",
"....##############################......",
"........................................"
};

const char* PIECE_KNIGHT[40] = {
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
"........................................",
".................##....##...............",
"................#OO#..#OO#..............",
"...............#OOOO##OOOO#.............",
"..............#OOOOOOOOOOOO#............",
".............#OOOOOOOOOOOOOO#...........",
"............#OOOOOOO##OOOOOOO#..........",
"...........#OOOOOOO#..#OOOOOOO#.........",
".......####OOOOOOOO#...#OOOOOOO#........",
"......#OOOOOOOOOOOO#...#OOOOOOO#........",
".....#OOOOOOOOOOOOO#....#OOOOOO#........",
".....#OOOOOOOOOOOOO#....#OOOOOO#........",
".....#OOOOOOOOOOOO#.....#OOOOOO#........",
"......#OOOOOOOOOO#......#OOOOOO#........",
".......##########......#OOOOOOO#........",
"..........#OOOOOO######OOOOOOOO#........",
".........#OOOOOOOOOOOOOOOOOOOOO#........",
"........#OOOOOOOOOOOOOOOOOOOOOO#........",
"........#OOOOOOOOOOOOOOOOOOOOOO#........",
".......#OOOOOOOOOOOOOOOOOOOOOO#.........",
".......#OOOOOOOOOOOOOOOOOOOOOO#.........",
"......#OOOOOOOOOOOOOOOOOOOOOO#..........",
"......#OOOOOOOOOOOOOOOOOOOOOO#..........",
"......#OOOOOOOOOOOOOOOOOOOOOO#..........",
"......#OOOOOOOOOOOOOOOOOOOOOO#..........",
"......#OOOOOOOOOOOOOOOOOOOOOO#..........",
"......#OOOOOOOOOOOOOOOOOOOOOO#..........",
"......#OOOOOOOOOOOOOOOOOOOOOO#..........",
"......########################..........",
".....#OOOOOOOOOOOOOOOOOOOOOOOO#.........",
".....#OOOOOOOOOOOOOOOOOOOOOOOO#.........",
"....#OOOOOOOOOOOOOOOOOOOOOOOOOO#........",
"....#OOOOOOOOOOOOOOOOOOOOOOOOOO#........",
"....############################........",
"........................................"
};

const char* PIECE_BISHOP[40] = {
"........................................",
"........................................",
"..................####..................",
".................#OOOO#.................",
".................######.................",
"................#OOOOOO#................",
"...............#OOOOOOOO#...............",
"..............#OOOOOOOOOO#..............",
".............#OOOOOOOOOOOO#.............",
"............#OOOOOOOOOOOOOO#............",
"...........#OOOOOOOOOOOOOOOO#...........",
"..........#OOOOOOOO##OOOOOOOO#..........",
".........#OOOOOOOO#..#OOOOOOOO#.........",
"........#OOOOOOOO#....#OOOOOOOO#........",
".......#OOOOOOOO#......#OOOOOOOO#.......",
".......#OOOOOOO#........#OOOOOOO#.......",
".......#OOOOOO#..........#OOOOOO#.......",
"........#OOOO#............#OOOO#........",
".........#OO#..............#OO#.........",
"..........##................##..........",
"...........#................#...........",
"............#..............#............",
".............##############.............",
"............#OOOOOOOOOOOOOO#............",
"............################............",
"...........#OOOOOOOOOOOOOOOO#...........",
"...........#OOOOOOOOOOOOOOOO#...........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
".........#OOOOOOOOOOOOOOOOOOOO#.........",
".........#OOOOOOOOOOOOOOOOOOOO#.........",
"........#OOOOOOOOOOOOOOOOOOOOOO#........",
"........#OOOOOOOOOOOOOOOOOOOOOO#........",
"........########################........",
".......#OOOOOOOOOOOOOOOOOOOOOOOO#.......",
".......#OOOOOOOOOOOOOOOOOOOOOOOO#.......",
"......#OOOOOOOOOOOOOOOOOOOOOOOOOO#......",
"......#OOOOOOOOOOOOOOOOOOOOOOOOOO#......",
"......############################......",
"........................................"
};

const char* PIECE_QUEEN[40] = {
"........................................",
"........................................",
"........................................",
"........................................",
".........##........##........##.........",
"........#OO#......#OO#......#OO#........",
"........####......####......####........",
".........##........##........##.........",
".........#O#......#OO#......#O#.........",
".........#OO#.....#OO#.....#OO#.........",
".........#OOO#....#OO#....#OOO#.........",
".........#OOOO#...#OO#...#OOOO#.........",
"..........#OOOO#..#OO#..#OOOO#..........",
"..........#OOOOO##OOOO##OOOOO#..........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
"...........#OOOOOOOOOOOOOOOO#...........",
"...........#OOOOOOOOOOOOOOOO#...........",
"...........#OOOOOOOOOOOOOOOO#...........",
"............#OOOOOOOOOOOOOO#............",
"............#OOOOOOOOOOOOOO#............",
"............################............",
".............#OOOOOOOOOOOO#.............",
".............##############.............",
"............#OOOOOOOOOOOOOO#............",
"............#OOOOOOOOOOOOOO#............",
"...........#OOOOOOOOOOOOOOOO#...........",
"...........#OOOOOOOOOOOOOOOO#...........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
".........#OOOOOOOOOOOOOOOOOOOO#.........",
".........#OOOOOOOOOOOOOOOOOOOO#.........",
"........#OOOOOOOOOOOOOOOOOOOOOO#........",
"........#OOOOOOOOOOOOOOOOOOOOOO#........",
"........########################........",
".......#OOOOOOOOOOOOOOOOOOOOOOOO#.......",
".......#OOOOOOOOOOOOOOOOOOOOOOOO#.......",
"......#OOOOOOOOOOOOOOOOOOOOOOOOOO#......",
"......#OOOOOOOOOOOOOOOOOOOOOOOOOO#......",
"......############################......",
"........................................"
};

const char* PIECE_KING[40] = {
"........................................",
"..................####..................",
".................#OOOO#.................",
"...........######OOOOOO######...........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
"...........######OOOOOO######...........",
".................#OOOO#.................",
"................########................",
"..............###OOOOOO###..............",
"............##OOOOOOOOOOOO##............",
"...........#OOOOOOOOOOOOOOOO#...........",
"..........#OOOOOOOO##OOOOOOOO#..........",
".........#OOOOOOO##..##OOOOOOO#.........",
"........#OOOOOOO#......#OOOOOOO#........",
"........#OOOOOO#........#OOOOOO#........",
"........#OOOOOO#........#OOOOOO#........",
"........#OOOOOO#........#OOOOOO#........",
".........#OOOOOO##....##OOOOOO#.........",
"..........#OOOOOOO####OOOOOOO#..........",
"...........#OOOOOOOOOOOOOOOO#...........",
"............#OOOOOOOOOOOOOO#............",
"............################............",
".............#OOOOOOOOOOOO#.............",
".............##############.............",
"............#OOOOOOOOOOOOOO#............",
"............#OOOOOOOOOOOOOO#............",
"...........#OOOOOOOOOOOOOOOO#...........",
"...........#OOOOOOOOOOOOOOOO#...........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
"..........#OOOOOOOOOOOOOOOOOO#..........",
".........#OOOOOOOOOOOOOOOOOOOO#.........",
".........#OOOOOOOOOOOOOOOOOOOO#.........",
"........#OOOOOOOOOOOOOOOOOOOOOO#........",
"........#OOOOOOOOOOOOOOOOOOOOOO#........",
"........########################........",
".......#OOOOOOOOOOOOOOOOOOOOOOOO#.......",
".......#OOOOOOOOOOOOOOOOOOOOOOOO#.......",
"......#OOOOOOOOOOOOOOOOOOOOOOOOOO#......",
"......############################......",
"........................................"
};
}

void PgnReaderActivity::renderBoard() const {
  renderer.clearScreen();
  
  int screenWidth = renderer.getScreenWidth();
  int screenHeight = renderer.getScreenHeight();
  
  int boardSize = std::min(screenWidth, screenHeight) - 20;
  int sqSize = boardSize / 8;
  boardSize = sqSize * 8;
  
  int startX = (screenWidth - boardSize) / 2;
  int startY = (screenHeight - boardSize) / 2;
  
  // Draw header (PGN title and current move)
  int titleY = startY / 2;
  renderer.drawCenteredText(SETTINGS.getReaderFontId(), titleY, gameTitle.c_str(), true);
  
  if (currentMoveIndex > 0 && currentMoveIndex - 1 < moveStrings.size()) {
    std::string moveText = "Move " + std::to_string((currentMoveIndex + 1) / 2) + (currentMoveIndex % 2 != 0 ? ". " : "... ") + moveStrings[currentMoveIndex - 1];
    renderer.drawCenteredText(SETTINGS.getReaderFontId(), startY + boardSize + (screenHeight - startY - boardSize) / 2, moveText.c_str(), true);
  }
  
  for (int rank = 0; rank < 8; rank++) {
    for (int file = 0; file < 8; file++) {
      int sqX = startX + file * sqSize;
      int sqY = startY + rank * sqSize;
      
      bool isDarkSquare = ((rank + file) % 2) != 0;
      if (isDarkSquare) {
        renderer.fillRectDither(sqX, sqY, sqSize, sqSize, Color::LightGray);
      } else {
        renderer.fillRect(sqX, sqY, sqSize, sqSize, false);
        renderer.drawRect(sqX, sqY, sqSize, sqSize, true);
      }
      
      int idx = rank * 8 + file;
      char piece = cr.squares[idx];
      
      if (piece != ' ') {
        const char** pieceData = nullptr;
        switch (std::tolower(piece)) {
          case 'p': pieceData = PIECE_PAWN; break;
          case 'r': pieceData = PIECE_ROOK; break;
          case 'n': pieceData = PIECE_KNIGHT; break;
          case 'b': pieceData = PIECE_BISHOP; break;
          case 'q': pieceData = PIECE_QUEEN; break;
          case 'k': pieceData = PIECE_KING; break;
        }
        
        if (pieceData) {
          bool isWhite = std::isupper(piece);
          int pieceSize = 40;
          int offsetX = sqX + (sqSize - pieceSize) / 2;
          int offsetY = sqY + (sqSize - pieceSize) / 2;
          
          for (int py = 0; py < pieceSize; py++) {
            for (int px = 0; px < pieceSize; px++) {
              char c = pieceData[py][px];
              if (c == '.') continue;
              
              bool drawBlack = (c == '#');
              if (!isWhite) {
                drawBlack = !drawBlack;
              }
              
              renderer.drawPixel(offsetX + px, offsetY + py, drawBlack);
            }
          }
        }
      }
    }
  }
}
