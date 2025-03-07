#include "solitaire.h"

size_t SolitaireGame::getDeckCount() const {
  switch (game_mode_) {
    case GameMode::STANDARD_KLONDIKE:
      return 1;
    case GameMode::DOUBLE_KLONDIKE:
      return 2;
    case GameMode::TRIPLE_KLONDIKE:
      return 3;
    default:
      return 1;
  }
}

// 3. Implement a method to set the game mode and update the game state
void SolitaireGame::setGameMode(GameMode mode) {
  if (game_mode_ == mode)
    return;
    
  game_mode_ = mode;
  
  // Regenerate a new random seed for a fresh game
  current_seed_ = rand();
  
  // Re-initialize the game with the new mode
  initializeGame();
  refreshDisplay();
}
