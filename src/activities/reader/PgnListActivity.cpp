#include "PgnListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "PgnReaderActivity.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

PgnListActivity::PgnListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Pgn> pgn)
    : Activity("PgnList", renderer, mappedInput), pgn(std::move(pgn)) {}

void PgnListActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void PgnListActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int itemCount = pgn->getGames().size();
  if (itemCount > 0) {
    buttonNavigator.onNext([this, itemCount] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this, itemCount] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
      requestUpdate();
    });
  }
}

void PgnListActivity::handleSelection() {
  const auto& games = pgn->getGames();
  if (selectedIndex < games.size()) {
    const auto& game = games[selectedIndex];
    std::string title = game.white + " vs " + game.black;
    if (title == " vs ") title = "Unknown vs Unknown";
    
    std::string moveText = pgn->getMoveText(game);
    activityManager.pushActivity(std::make_unique<PgnReaderActivity>(renderer, mappedInput, title, moveText));
  }
}

void PgnListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, pgn->getTitle().c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  
  const auto& games = pgn->getGames();
  const int itemCount = games.size();

  if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No games found");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
        [&games](int index) {
          std::string title = games[index].white + " vs " + games[index].black;
          if (title == " vs ") title = "Unknown vs Unknown";
          return title;
        },
        [&games](int index) {
          return games[index].event;
        });
  }

  const auto labels = mappedInput.mapLabels("Back", "Read", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
