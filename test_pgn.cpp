#include "lib/Thc/thc.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cctype>

int main() {
    std::ifstream file("small.pgn");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    int i = 0;
    int n = content.length();
    
    thc::ChessRules parserCr;
    std::vector<thc::Move> moves;
    
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
        parserCr.PlayMove(m);
      } else {
        std::cout << "Failed to parse token: " << token << std::endl;
      }
    }

    std::cout << "Parsed " << moves.size() << " moves." << std::endl;
    thc::ChessRules cr;
    cr.Init();
    for (int i=0; i<moves.size(); i++) {
        cr.PlayMove(moves[i]);
    }
    std::cout << "Final board:" << std::endl << cr.ToDebugStr() << std::endl;
    return 0;
}
