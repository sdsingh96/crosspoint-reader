#include "MdReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x4D445244;  // "MDRD"
constexpr uint8_t CACHE_VERSION = 2;          // Increment when cache format changes

std::vector<MdSegment> parseMarkdownInline(const std::string& line, EpdFontFamily::Style baseStyle) {
    std::vector<MdSegment> segments;
    size_t i = 0;
    size_t len = line.length();
    
    std::string currentText;
    bool isBold = (baseStyle & EpdFontFamily::BOLD) != 0;
    bool isItalic = (baseStyle & EpdFontFamily::ITALIC) != 0;

    auto pushSegment = [&]() {
        if (!currentText.empty()) {
            EpdFontFamily::Style style = EpdFontFamily::REGULAR;
            if (isBold && isItalic) style = EpdFontFamily::BOLD_ITALIC;
            else if (isBold) style = EpdFontFamily::BOLD;
            else if (isItalic) style = EpdFontFamily::ITALIC;
            
            segments.push_back({currentText, style});
            currentText.clear();
        }
    };

    while (i < len) {
        if (line[i] == '\\' && i + 1 < len) {
            currentText += line[i+1];
            i += 2;
        } else if (i + 1 < len && ((line[i] == '*' && line[i+1] == '*') || (line[i] == '_' && line[i+1] == '_'))) {
            pushSegment();
            isBold = !isBold;
            i += 2;
        } else if (line[i] == '*' || line[i] == '_') {
            pushSegment();
            isItalic = !isItalic;
            i += 1;
        } else {
            currentText += line[i];
            i++;
        }
    }
    pushSegment();
    
    if (segments.empty()) {
        segments.push_back({"", baseStyle});
    }
    return segments;
}

std::vector<std::vector<MdSegment>> parseTableCells(const std::string& line, EpdFontFamily::Style baseStyle) {
    std::vector<std::vector<MdSegment>> cells;
    size_t start = 0;
    if (line.front() == '|') start = 1;
    
    size_t len = line.length();
    size_t end = len;
    if (len > 0 && line.back() == '|') end = len - 1;
    
    std::string currentCell;
    for (size_t i = start; i < end; ++i) {
        if (line[i] == '\\' && i + 1 < end && line[i+1] == '|') {
            currentCell += '|';
            i++;
        } else if (line[i] == '|') {
            size_t cellStart = 0;
            while(cellStart < currentCell.length() && currentCell[cellStart] == ' ') cellStart++;
            size_t cellEnd = currentCell.length();
            while(cellEnd > cellStart && currentCell[cellEnd-1] == ' ') cellEnd--;
            
            cells.push_back(parseMarkdownInline(currentCell.substr(cellStart, cellEnd - cellStart), baseStyle));
            currentCell.clear();
        } else {
            currentCell += line[i];
        }
    }
    size_t cellStart = 0;
    while(cellStart < currentCell.length() && currentCell[cellStart] == ' ') cellStart++;
    size_t cellEnd = currentCell.length();
    while(cellEnd > cellStart && currentCell[cellEnd-1] == ' ') cellEnd--;
    cells.push_back(parseMarkdownInline(currentCell.substr(cellStart, cellEnd - cellStart), baseStyle));
    return cells;
}

bool isTableSeparator(const std::string& line) {
    if (line.empty() || line.find('|') == std::string::npos) return false;
    for (char c : line) {
        if (c != '|' && c != '-' && c != ' ' && c != ':') return false;
    }
    return true;
}

}  // namespace

void MdReaderActivity::onEnter() {
  Activity::onEnter();

  if (!md) {
    return;
  }

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  md->setupCacheDir();

  // Save current md as last opened file and add to recent books
  auto filePath = md->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");

  // Trigger first update
  requestUpdate();
}

void MdReaderActivity::onExit() {
  Activity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  pageOffsets.clear();
  currentPageLines.clear();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  md.reset();
}

void MdReaderActivity::loop() {
  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    activityManager.goToFileBrowser(md ? md->getPath() : "");
    return;
  }

  // Short press BACK goes directly to home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    onGoHome();
    return;
  }

  const auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (prevTriggered && currentPage > 0) {
    currentPage--;
    requestUpdate();
  } else if (nextTriggered) {
    if (currentPage < totalPages - 1) {
      currentPage++;
      requestUpdate();
    } else {
      onGoHome();
    }
  }
}

void MdReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("MDRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  // Try to load cached page index first
  if (!loadPageIndexCache()) {
    // Cache not found, build page index
    buildPageIndex();
    // Save to cache for next time
    savePageIndexCache();
  }

  // Load saved progress
  loadProgress();

  initialized = true;
}

void MdReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = md->getFileSize();

  LOG_DBG("MDRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<MdLine> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("MDRS", "Built page index: %d pages", totalPages);
}

bool MdReaderActivity::loadPageAtOffset(size_t offset, std::vector<MdLine>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = md->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("MDRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!md->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x0F);
  }

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    // Calculate the actual length of line content in the buffer (excluding newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    // Extract line content for display (without CR/LF)
    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);

    // Track position within this source line (in bytes from pos)
    size_t lineBytePos = 0;

    // Parse markdown styling for this line
    EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
    int indentation = 0;
    bool isCheckbox = false;
    bool isChecked = false;
    
    // Calculate leading whitespace indentation
    size_t wsCount = 0;
    while (wsCount < line.length() && (line[wsCount] == ' ' || line[wsCount] == '\t')) {
        if (line[wsCount] == ' ') indentation += 10;
        else if (line[wsCount] == '\t') indentation += 30; // approx 3 spaces
        wsCount++;
    }
    
    // Strip leading whitespace for matching
    if (wsCount > 0) {
        line = line.substr(wsCount);
        lineBytePos += wsCount;
    }
    
    if (line.rfind("# ", 0) == 0) {
        fontStyle = EpdFontFamily::BOLD;
        line = line.substr(2);
        lineBytePos += 2;
    } else if (line.rfind("## ", 0) == 0) {
        fontStyle = EpdFontFamily::BOLD;
        line = line.substr(3);
        lineBytePos += 3;
    } else if (line.rfind("### ", 0) == 0) {
        fontStyle = EpdFontFamily::BOLD;
        line = line.substr(4);
        lineBytePos += 4;
    } else if (line.rfind("> ", 0) == 0) {
        fontStyle = EpdFontFamily::ITALIC;
        indentation += 20;
        line = line.substr(2);
        lineBytePos += 2;
    } else if (line.rfind("- [ ] ", 0) == 0) {
        isCheckbox = true;
        isChecked = false;
        indentation += 10;
        line = line.substr(6);
        lineBytePos += 6;
    } else if (line.rfind("- [x] ", 0) == 0 || line.rfind("- [X] ", 0) == 0) {
        isCheckbox = true;
        isChecked = true;
        indentation += 10;
        line = line.substr(6);
        lineBytePos += 6;
    } else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
        line = "* " + line.substr(2); // standard ascii bullet
        indentation += 10;
        lineBytePos += 2; 
    }

    bool isTable = false;
    bool isTableSep = false;
    bool isHr = false;
    std::vector<std::vector<MdSegment>> tableCells;
    
    // Check for Horizontal Rule
    if (line.length() >= 3 && 
        (line.find_first_not_of("- ") == std::string::npos ||
         line.find_first_not_of("* ") == std::string::npos ||
         line.find_first_not_of("_ ") == std::string::npos)) {
        int charCount = 0;
        for (char c : line) if (c == '-' || c == '*' || c == '_') charCount++;
        if (charCount >= 3) {
            isHr = true;
        }
    }
    
    if (!isHr && line.find('|') != std::string::npos && line.length() > 2) {
        isTableSep = isTableSeparator(line);
        if (isTableSep || line.front() == '|' || line.find(" | ") != std::string::npos) {
            isTable = true;
            if (!isTableSep) {
                tableCells = parseTableCells(line, fontStyle);
            }
        }
    }

    bool fullyConsumed = false;
    
    if (isHr) {
        MdLine mdLine;
        mdLine.indentation = indentation;
        mdLine.isHorizontalRule = true;
        outLines.push_back(mdLine);
        lineBytePos = displayLen;
        fullyConsumed = true;
    } else if (isTable) {
        MdLine mdLine;
        mdLine.indentation = indentation;
        mdLine.isTable = true;
        mdLine.isTableSeparator = isTableSep;
        mdLine.tableCells = tableCells;
        outLines.push_back(mdLine);
        lineBytePos = displayLen;
        fullyConsumed = true;
    } else {
        std::vector<MdSegment> sourceSegments = parseMarkdownInline(line, fontStyle);
        
        do {
            if (sourceSegments.empty() || (sourceSegments.size() == 1 && sourceSegments[0].text.empty())) {
                MdLine mdLine;
                mdLine.segments.push_back({"", fontStyle});
                mdLine.indentation = indentation;
                mdLine.isCheckbox = isCheckbox;
                mdLine.isChecked = isChecked;
                outLines.push_back(mdLine);
                sourceSegments.clear();
                break;
            }

            int maxLineWidth = viewportWidth - indentation;
            int currentLineWidth = 0;
            
            MdLine currentVisualLine;
            currentVisualLine.indentation = indentation;
            currentVisualLine.isCheckbox = isCheckbox;
            currentVisualLine.isChecked = isChecked;
            isCheckbox = false; // only first line gets checkbox
            
            size_t segIdx = 0;
            bool wrapped = false;
            size_t breakPosInSegment = 0;
            
            while (segIdx < sourceSegments.size()) {
                const auto& seg = sourceSegments[segIdx];
                int segWidth = renderer.getTextAdvanceX(cachedFontId, seg.text.c_str(), seg.fontStyle);
                
                if (currentLineWidth + segWidth <= maxLineWidth) {
                    currentVisualLine.segments.push_back(seg);
                    currentLineWidth += segWidth;
                    segIdx++;
                } else {
                    size_t breakPos = seg.text.length();
                    while (breakPos > 0) {
                        int partialWidth = renderer.getTextAdvanceX(cachedFontId, seg.text.substr(0, breakPos).c_str(), seg.fontStyle);
                        if (currentLineWidth + partialWidth <= maxLineWidth) {
                            size_t spacePos = seg.text.rfind(' ', breakPos - 1);
                            if (spacePos != std::string::npos && spacePos > 0) {
                                breakPos = spacePos;
                            } else if (currentLineWidth > 0) {
                                breakPos = 0;
                            } else {
                                breakPos--;
                                while (breakPos > 0 && (seg.text[breakPos] & 0xC0) == 0x80) {
                                    breakPos--;
                                }
                                if (breakPos == 0) breakPos = 1;
                            }
                            break;
                        }
                        breakPos--;
                    }
                    
                    if (breakPos == 0 && currentLineWidth > 0) {
                        breakPosInSegment = 0;
                    } else {
                        if (breakPos > 0) {
                            currentVisualLine.segments.push_back({seg.text.substr(0, breakPos), seg.fontStyle});
                        }
                        breakPosInSegment = breakPos;
                    }
                    wrapped = true;
                    break;
                }
            }
            
            outLines.push_back(currentVisualLine);
            
            if (!wrapped) {
                lineBytePos = displayLen;
                sourceSegments.clear();
            } else {
                size_t consumedChars = 0;
                for (size_t i = 0; i < segIdx; ++i) consumedChars += sourceSegments[i].text.length();
                consumedChars += breakPosInSegment;
                
                size_t skipChars = 0;
                if (breakPosInSegment < sourceSegments[segIdx].text.length() && sourceSegments[segIdx].text[breakPosInSegment] == ' ') {
                    skipChars = 1;
                }
                
                lineBytePos += consumedChars + skipChars;
                
                std::vector<MdSegment> remainingSegments;
                if (breakPosInSegment + skipChars < sourceSegments[segIdx].text.length()) {
                    remainingSegments.push_back({sourceSegments[segIdx].text.substr(breakPosInSegment + skipChars), sourceSegments[segIdx].fontStyle});
                }
                for (size_t i = segIdx + 1; i < sourceSegments.size(); ++i) {
                    remainingSegments.push_back(sourceSegments[i]);
                }
                sourceSegments = remainingSegments;
            }
        } while (!sourceSegments.empty() && static_cast<int>(outLines.size()) < linesPerPage);
        
        if (sourceSegments.empty()) fullyConsumed = true;
    }

    // Determine how much of the source buffer we consumed
    if (fullyConsumed) {
      // Fully consumed this source line, move past the newline
      pos = lineEnd + 1;
    } else {
      // Partially consumed - page is full mid-line
      // Move pos to where we stopped in the line (NOT past the line)
      // Since lineBytePos might be skewed from bullet point expansion, just cap it
      if (lineBytePos > displayLen) lineBytePos = displayLen;
      pos = pos + lineBytePos;
      break;
    }
  }

  // Ensure we make progress even if calculations go wrong
  if (pos == 0 && !outLines.empty()) {
    // Fallback: at minimum, consume something to avoid infinite loop
    pos = 1;
  }

  nextOffset = offset + pos;

  // Make sure we don't go past the file
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);

  return !outLines.empty();
}

void MdReaderActivity::render(RenderLock&&) {
  if (!md) {
    return;
  }

  // Initialize reader if not done
  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();

  // Save progress
  saveProgress();
}

void MdReaderActivity::renderPage() {
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& mdLine : currentPageLines) {
      if (mdLine.isHorizontalRule) {
        int x = cachedOrientedMarginLeft + mdLine.indentation;
        int contentWidth = viewportWidth - mdLine.indentation;
        renderer.drawLine(x, y + lineHeight / 2, x + contentWidth, y + lineHeight / 2, true);
        y += lineHeight;
        continue;
      }
      
      if (mdLine.isTableSeparator) {
        int x = cachedOrientedMarginLeft + mdLine.indentation;
        int contentWidth = viewportWidth - mdLine.indentation;
        renderer.drawLine(x, y + lineHeight / 2, x + contentWidth, y + lineHeight / 2, true);
        y += lineHeight;
        continue;
      }
      
      if (mdLine.isTable) {
        int x = cachedOrientedMarginLeft + mdLine.indentation;
        int contentWidth = viewportWidth - mdLine.indentation;
        int numCols = mdLine.tableCells.size();
        if (numCols > 0) {
          int colWidth = contentWidth / numCols;
          for (int c = 0; c < numCols; ++c) {
             int cellX = x + c * colWidth;
             int currentX = cellX + 5; // padding
             for (const auto& seg : mdLine.tableCells[c]) {
                 int segWidth = renderer.getTextAdvanceX(cachedFontId, seg.text.c_str(), seg.fontStyle);
                 if (currentX + segWidth > cellX + colWidth - 5) {
                     // Simple truncation
                     renderer.drawText(cachedFontId, currentX, y, seg.text.c_str(), true, seg.fontStyle);
                     break; 
                 } else {
                     renderer.drawText(cachedFontId, currentX, y, seg.text.c_str(), true, seg.fontStyle);
                     currentX += segWidth;
                 }
             }
             renderer.drawLine(cellX, y, cellX, y + lineHeight, true);
          }
          renderer.drawLine(x + contentWidth, y, x + contentWidth, y + lineHeight, true); // right border
        }
        y += lineHeight;
        continue;
      }

      if (!mdLine.segments.empty() || mdLine.isCheckbox) {
        const int contentWidth = viewportWidth - mdLine.indentation;
        int x = cachedOrientedMarginLeft + mdLine.indentation;
        int checkboxOffset = mdLine.isCheckbox ? 20 : 0;
        
        std::string fullText;
        for (const auto& seg : mdLine.segments) fullText += seg.text;
        
        const bool lineIsRtl = BidiUtils::startsWithRtl(fullText.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        
        int textWidth = 0;
        for (const auto& seg : mdLine.segments) {
            textWidth += renderer.getTextAdvanceX(cachedFontId, seg.text.c_str(), seg.fontStyle);
        }

        // Apply text alignment
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            x += checkboxOffset;
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            x = cachedOrientedMarginLeft + mdLine.indentation + (contentWidth - textWidth) / 2 + checkboxOffset;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + mdLine.indentation + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            x += checkboxOffset;
            break;
        }

        if (mdLine.isCheckbox) {
           int boxSize = 14;
           int boxX = x - checkboxOffset;
           int boxY = y + (lineHeight - boxSize) / 2;
           renderer.drawRect(boxX, boxY, boxSize, boxSize, true); // Black outline
           if (mdLine.isChecked) {
             renderer.drawLine(boxX, boxY, boxX + boxSize, boxY + boxSize, true);
             renderer.drawLine(boxX + boxSize, boxY, boxX, boxY + boxSize, true);
           }
        }

        int currentX = x;
        for (const auto& seg : mdLine.segments) {
          if (!seg.text.empty()) {
            renderer.drawText(cachedFontId, currentX, y, seg.text.c_str(), true, seg.fontStyle);
            currentX += renderer.getTextAdvanceX(cachedFontId, seg.text.c_str(), seg.fontStyle);
          }
        }
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();  // scan pass — text accumulated, no drawing
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines();
  renderStatusBar();

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  }
}

void MdReaderActivity::renderStatusBar() const {
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  std::string title;
  if (SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) {
    title = md->getTitle();
  }
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title);
}

void MdReaderActivity::saveProgress() const {
  HalFile f;
  if (Storage.openFileForWrite("MDRS", md->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = 0;
    data[3] = 0;
    f.write(data, 4);
  }
}

void MdReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("MDRS", md->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      LOG_DBG("MDRS", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
  }
}

bool MdReaderActivity::loadPageIndexCache() {
  std::string cachePath = md->getCachePath() + "/md_index.bin";
  HalFile f;
  if (!Storage.openFileForRead("MDRS", cachePath, f)) {
    LOG_DBG("MDRS", "No page index cache found");
    return false;
  }

  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("MDRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("MDRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != md->getFileSize()) {
    LOG_DBG("MDRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("MDRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    LOG_DBG("MDRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("MDRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    LOG_DBG("MDRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("MDRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  uint32_t numPages;
  serialization::readPod(f, numPages);

  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }

  totalPages = pageOffsets.size();
  LOG_DBG("MDRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void MdReaderActivity::savePageIndexCache() const {
  std::string cachePath = md->getCachePath() + "/md_index.bin";
  HalFile f;
  if (!Storage.openFileForWrite("MDRS", cachePath, f)) {
    LOG_ERR("MDRS", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(md->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  LOG_DBG("MDRS", "Saved page index cache: %d pages", totalPages);
}

ScreenshotInfo MdReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (md) {
    const std::string t = md->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
