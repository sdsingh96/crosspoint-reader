#pragma once

#include <Txt.h>

#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include <EpdFontFamily.h>

struct MdSegment {
    std::string text;
    EpdFontFamily::Style fontStyle;
};

struct MdLine {
    std::vector<MdSegment> segments;
    int indentation = 0;
    bool isCheckbox = false;
    bool isChecked = false;

    // Table support
    bool isTable = false;
    bool isTableSeparator = false;
    std::vector<std::vector<MdSegment>> tableCells;

    // Horizontal Rule
    bool isHorizontalRule = false;
};

class MdReaderActivity final : public Activity {
  std::unique_ptr<Txt> md;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  std::vector<MdLine> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage();
  void renderStatusBar() const;

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<MdLine>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();

 public:
  explicit MdReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> md)
      : Activity("MdReader", renderer, mappedInput), md(std::move(md)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};
