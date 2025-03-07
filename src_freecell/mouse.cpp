#include "freecell.h"
#include <gtk/gtk.h>

#include "freecell.h"
#include <gtk/gtk.h>

gboolean FreecellGame::onButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);

  // Reset keyboard navigation when using mouse
  game->keyboard_navigation_active_ = false;
  game->keyboard_selection_active_ = false;

  // If win animation is active, stop it
  if (game->win_animation_active_) {
    game->stopWinAnimation();
    return TRUE;
  }
  if (game->foundation_move_animation_active_ || game->deal_animation_active_) {
    return TRUE;
  }

  // Determine number of freecells based on game mode
  int num_freecells = (game->current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 4 : 6;
  int foundation_start = num_freecells;
  int foundation_end = foundation_start + 4; // Always 4 foundation piles
  int tableau_start = foundation_end;

  if (event->button == 1) { // Left click
    auto [pile_index, card_index] = game->getPileAt(event->x, event->y);

    if (pile_index >= 0) {
      // Handle starting a drag operation
      if (game->isValidDragSource(pile_index, card_index)) {
        game->dragging_ = true;
        game->drag_source_pile_ = pile_index;
        game->drag_source_card_idx_ = card_index; // Store the source card index
        game->drag_start_x_ = event->x;
        game->drag_start_y_ = event->y;
        game->drag_cards_.clear(); // Clear any previous drag sequence
        
        // Get the card being dragged
        if (pile_index < num_freecells) {
          // Dragging from freecell
          game->drag_card_ = game->freecells_[pile_index];
          // For freecell, only one card is dragged
          if (game->drag_card_.has_value()) {
            game->drag_cards_.push_back(game->drag_card_.value());
          }
          
          // Calculate offset from top-left of card
          game->drag_offset_x_ = event->x - (game->current_card_spacing_ + 
                                 pile_index * (game->current_card_width_ + game->current_card_spacing_));
          game->drag_offset_y_ = event->y - game->current_card_spacing_;
        }
        else if (pile_index >= foundation_start && pile_index < foundation_end) {
          // Dragging from foundation - only top card
          int foundation_idx = pile_index - foundation_start;
          if (!game->foundation_[foundation_idx].empty()) {
            game->drag_card_ = game->foundation_[foundation_idx].back();
            // For foundation, only one card is dragged
            game->drag_cards_.push_back(game->drag_card_.value());
            
            // Calculate offset from top-left of card
            int x_pos = game->allocation.width - (4 - foundation_idx) * 
                        (game->current_card_width_ + game->current_card_spacing_);
            game->drag_offset_x_ = event->x - x_pos;
            game->drag_offset_y_ = event->y - game->current_card_spacing_;
          }
        }
        else if (pile_index >= tableau_start) {
          // Dragging from tableau
          int tableau_idx = pile_index - tableau_start;
          if (tableau_idx >= 0 && tableau_idx < game->tableau_.size() &&
              card_index >= 0 && card_index < game->tableau_[tableau_idx].size()) {
            // Store top card for visual representation during drag
            game->drag_card_ = game->tableau_[tableau_idx][card_index];
            
            // Store all cards from the selected index to the bottom
            for (size_t i = card_index; i < game->tableau_[tableau_idx].size(); i++) {
              game->drag_cards_.push_back(game->tableau_[tableau_idx][i]);
            }
            
            // Calculate offset from top-left of card
            game->drag_offset_x_ = event->x - (game->current_card_spacing_ + 
                                   tableau_idx * (game->current_card_width_ + game->current_card_spacing_));
            game->drag_offset_y_ = event->y - (2 * game->current_card_spacing_ + 
                                   game->current_card_height_ + card_index * game->current_vert_spacing_);
          }
        }
        
        // Play sound for card pickup
        game->playSound(GameSoundEvent::CardFlip);
      }
    }
  } 
  else if (event->button == 3) { // Right click
    auto [pile_index, card_index] = game->getPileAt(event->x, event->y);

    // Try to automatically move card to foundation
    if (pile_index >= 0) {
      // Source: freecell
      if (pile_index < num_freecells && game->freecells_[pile_index].has_value()) {
        const cardlib::Card card = game->freecells_[pile_index].value();
        
        // Try to find a valid foundation
        int target_foundation = -1;
        for (int i = 0; i < 4; i++) {
          if (game->canMoveToFoundation(card, i)) {
            target_foundation = i;
            break;
          }
        }
        
        if (target_foundation != -1) {
          // Move card to foundation
          game->foundation_[target_foundation].push_back(card);
          game->freecells_[pile_index] = std::nullopt;
          
          // Play sound
          game->playSound(GameSoundEvent::CardPlace);
          
          // Check for win
          if (game->checkWinCondition()) {
            game->startWinAnimation();
          }
          
          game->refreshDisplay();
          return TRUE;
        }
        // If cannot move to foundation, we don't need to check for freecell
        // as the card is already in a freecell
      }
      // Source: tableau
      else if (pile_index >= tableau_start) {
        int tableau_idx = pile_index - tableau_start;
        if (tableau_idx >= 0 && tableau_idx < game->tableau_.size()) {
          auto &pile = game->tableau_[tableau_idx];
          
          if (!pile.empty()) {
            const cardlib::Card &card = pile.back();
            
            // Try to find a valid foundation
            int target_foundation = -1;
            for (int i = 0; i < 4; i++) {
              if (game->canMoveToFoundation(card, i)) {
                target_foundation = i;
                break;
              }
            }
            
            if (target_foundation != -1) {
              // Move card to foundation
              game->foundation_[target_foundation].push_back(card);
              pile.pop_back();
              
              // Play sound
              game->playSound(GameSoundEvent::CardPlace);
              
              // Check for win
              if (game->checkWinCondition()) {
                game->startWinAnimation();
              }
              
              game->refreshDisplay();
              return TRUE;
            } 
            else {
              // If cannot move to foundation, try to move to the first available freecell
              int target_freecell = -1;
              for (int i = 0; i < num_freecells; i++) {
                if (!game->freecells_[i].has_value()) {
                  target_freecell = i;
                  break;
                }
              }
              
              if (target_freecell != -1) {
                // Move card to freecell
                game->freecells_[target_freecell] = card;
                pile.pop_back();
                
                // Play sound
                game->playSound(GameSoundEvent::CardPlace);
                
                game->refreshDisplay();
                return TRUE;
              }
            }
          }
        }
      }
    }
  }

  return TRUE;
}

gboolean FreecellGame::onButtonRelease(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);

  if (event->button == 1 && game->dragging_) {
    auto [target_pile, target_card_index] = game->getPileAt(event->x, event->y);

    // Determine pile indices based on game mode
    int num_freecells = (game->current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 4 : 6;
    int foundation_start = num_freecells;
    int foundation_end = foundation_start + 4; // Always 4 foundation piles
    int tableau_start = foundation_end;

    if (target_pile >= 0 && game->drag_card_.has_value()) {
      bool move_successful = false;

      // Handle dropping on freecell (0 to num_freecells-1)
      if (target_pile < num_freecells) {
        // Can only drop a single card on an empty freecell
        if (!game->freecells_[target_pile].has_value() && game->drag_cards_.size() == 1) {
          // Move card to freecell
          game->freecells_[target_pile] = game->drag_cards_[0];
          
          // Remove from source
          if (game->drag_source_pile_ < num_freecells) {
            game->freecells_[game->drag_source_pile_] = std::nullopt;
          }
          else if (game->drag_source_pile_ >= foundation_start && game->drag_source_pile_ < foundation_end) {
            int foundation_idx = game->drag_source_pile_ - foundation_start;
            if (!game->foundation_[foundation_idx].empty()) {
              game->foundation_[foundation_idx].pop_back();
            }
          }
          else if (game->drag_source_pile_ >= tableau_start) {
            int tableau_idx = game->drag_source_pile_ - tableau_start;
            if (tableau_idx >= 0 && tableau_idx < game->tableau_.size() && !game->tableau_[tableau_idx].empty()) {
              game->tableau_[tableau_idx].pop_back();
            }
          }
          
          move_successful = true;
        }
      }
      // Handle dropping on foundation (foundation_start to foundation_end-1)
      else if (target_pile >= foundation_start && target_pile < foundation_end) {
        int foundation_idx = target_pile - foundation_start;
        
        // Can only move a single card to foundation
        if (foundation_idx >= 0 && foundation_idx < game->foundation_.size() &&
            game->drag_cards_.size() == 1 && 
            game->canMoveToFoundation(game->drag_cards_[0], foundation_idx)) {
          // Add to foundation
          game->foundation_[foundation_idx].push_back(game->drag_cards_[0]);
          
          // Remove from source
          if (game->drag_source_pile_ < num_freecells) {
            game->freecells_[game->drag_source_pile_] = std::nullopt;
          }
          else if (game->drag_source_pile_ >= foundation_start && game->drag_source_pile_ < foundation_end) {
            int source_foundation = game->drag_source_pile_ - foundation_start;
            if (source_foundation >= 0 && source_foundation < game->foundation_.size() && 
                !game->foundation_[source_foundation].empty()) {
              game->foundation_[source_foundation].pop_back();
            }
          }
          else if (game->drag_source_pile_ >= tableau_start) {
            int tableau_idx = game->drag_source_pile_ - tableau_start;
            if (tableau_idx >= 0 && tableau_idx < game->tableau_.size() && 
                !game->tableau_[tableau_idx].empty()) {
              game->tableau_[tableau_idx].pop_back();
            }
          }
          
          move_successful = true;
        }
      }
      // Handle dropping on tableau (tableau_start and beyond)
      else if (target_pile >= tableau_start) {
        int tableau_idx = target_pile - tableau_start;
        
        if (tableau_idx >= 0 && tableau_idx < game->tableau_.size()) {
          // If dragging from tableau with multiple cards
          if (game->drag_source_pile_ >= tableau_start && game->drag_cards_.size() > 0) {
            // Check if we can move the entire stack
            if (game->canMoveTableauStack(game->drag_cards_, tableau_idx)) {
              // Add all cards to destination tableau
              for (const auto& card : game->drag_cards_) {
                game->tableau_[tableau_idx].push_back(card);
              }
              
              // Remove cards from source tableau
              int source_tableau = game->drag_source_pile_ - tableau_start;
              if (source_tableau >= 0 && source_tableau < game->tableau_.size() && 
                  !game->tableau_[source_tableau].empty() &&
                  game->drag_source_card_idx_ >= 0 && 
                  game->drag_source_card_idx_ <= game->tableau_[source_tableau].size()) {
                // Remove the cards from drag_source_card_idx_ to the end
                game->tableau_[source_tableau].erase(
                  game->tableau_[source_tableau].begin() + game->drag_source_card_idx_,
                  game->tableau_[source_tableau].end()
                );
              }
              
              move_successful = true;
            }
          }
          // Single card move (from freecell, foundation, or tableau)
          else if (game->drag_cards_.size() == 1 && 
                  game->canMoveToTableau(game->drag_cards_[0], tableau_idx)) {
            // Add to tableau
            game->tableau_[tableau_idx].push_back(game->drag_cards_[0]);
            
            // Remove from source
            if (game->drag_source_pile_ < num_freecells) {
              game->freecells_[game->drag_source_pile_] = std::nullopt;
            }
            else if (game->drag_source_pile_ >= foundation_start && game->drag_source_pile_ < foundation_end) {
              int source_foundation = game->drag_source_pile_ - foundation_start;
              if (source_foundation >= 0 && source_foundation < game->foundation_.size() && 
                  !game->foundation_[source_foundation].empty()) {
                game->foundation_[source_foundation].pop_back();
              }
            }
            else if (game->drag_source_pile_ >= tableau_start) {
              int source_tableau = game->drag_source_pile_ - tableau_start;
              if (source_tableau >= 0 && source_tableau < game->tableau_.size() && 
                  !game->tableau_[source_tableau].empty()) {
                game->tableau_[source_tableau].pop_back();
              }
            }
            
            move_successful = true;
          }
        }
      }

      if (move_successful) {
        // Play sound for successful move
        game->playSound(GameSoundEvent::CardPlace);
        
        // Check for win
        if (game->checkWinCondition()) {
          game->startWinAnimation();
        }
      }
    }

    // Reset drag state
    game->dragging_ = false;
    game->drag_card_ = std::nullopt;
    game->drag_cards_.clear();
    game->drag_source_pile_ = -1;
    game->drag_source_card_idx_ = -1;
    
    // Refresh display
    game->refreshDisplay();
  }

  return TRUE;
}

// Corrected to return std::pair<int, int> instead of void
std::pair<int, int> FreecellGame::getPileAt(int x, int y) const {
  // Get widget dimensions
  GtkAllocation allocation;
  gtk_widget_get_allocation(game_area_, &allocation);
  
  // Determine number of freecells based on game mode
  int num_freecells = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 4 : 6;
  
  // Check freecells
  int freecell_y = current_card_spacing_;
  for (int i = 0; i < num_freecells; i++) {
    int pile_x = current_card_spacing_ + i * (current_card_width_ + current_card_spacing_);
    if (x >= pile_x && x <= pile_x + current_card_width_ &&
        y >= freecell_y && y <= freecell_y + current_card_height_) {
      return {i, 0}; // Freecell piles are index 0-(num_freecells-1)
    }
  }
  
  // Foundation piles are always 4, but positioned differently
  int foundation_start_x;
  if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
    foundation_start_x = allocation.width - 4 * (current_card_width_ + current_card_spacing_);
  } else {
    foundation_start_x = allocation.width - 4 * (current_card_width_ + current_card_spacing_);
  }
  
  // Check foundation piles (4-7)
  int foundation_x = foundation_start_x;
  for (int i = 0; i < 4; i++) {
    if (x >= foundation_x && x <= foundation_x + current_card_width_ &&
        y >= freecell_y && y <= freecell_y + current_card_height_) {
      return {num_freecells + i, foundation_[i].empty() ? -1 : static_cast<int>(foundation_[i].size() - 1)};
    }
    foundation_x += current_card_width_ + current_card_spacing_;
  }
  
  // Number of tableau columns depends on game mode
  int num_tableau_columns = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 8 : 10;
  
  // Check tableau piles (starts after foundations)
  int tableau_start_idx = num_freecells + 4; // After freecells and foundations
  int tableau_y = 2 * current_card_spacing_ + current_card_height_;
  
  for (int i = 0; i < num_tableau_columns; i++) {
    int pile_x = current_card_spacing_ + i * (current_card_width_ + current_card_spacing_);
    if (x >= pile_x && x <= pile_x + current_card_width_) {
      const auto &pile = tableau_[i];
      
      if (pile.empty() && y >= tableau_y && y <= tableau_y + current_card_height_) {
        return {tableau_start_idx + i, -1}; // Empty tableau pile
      }
      
      // Check each card from bottom to top (later cards overlay earlier ones)
      for (int j = static_cast<int>(pile.size()) - 1; j >= 0; j--) {
        int card_y = tableau_y + j * current_vert_spacing_;
        if (y >= card_y && y <= card_y + current_card_height_) {
          return {tableau_start_idx + i, j};
        }
      }
    }
  }
  
  return {-1, -1}; // No pile found at this position
}

bool FreecellGame::isValidDragSource(int pile_index, int card_index) const {
  if (pile_index < 0) {
    return false;
  }
  
  // Determine pile indices based on game mode
  int num_freecells = (current_game_mode_ == GameMode::CLASSIC_FREECELL) ? 4 : 6;
  int foundation_start = num_freecells;
  int foundation_end = foundation_start + 4; // Always 4 foundation piles
  int tableau_start = foundation_end;
  
  // From freecell - can drag if not empty
  if (pile_index < num_freecells) {
    return pile_index >= 0 && pile_index < freecells_.size() && freecells_[pile_index].has_value();
  }
  
  // From foundation - can only drag top card
  if (pile_index >= foundation_start && pile_index < foundation_end) {
    int foundation_idx = pile_index - foundation_start;
    return foundation_idx >= 0 && foundation_idx < foundation_.size() && 
           !foundation_[foundation_idx].empty() && 
           static_cast<size_t>(card_index) == foundation_[foundation_idx].size() - 1;
  }
  
  // From tableau - can drag any card that forms a valid sequence to the bottom
  if (pile_index >= tableau_start) {
    int tableau_idx = pile_index - tableau_start;
    
    if (tableau_idx < 0 || static_cast<size_t>(tableau_idx) >= tableau_.size() || 
        tableau_[tableau_idx].empty() || card_index < 0 || 
        static_cast<size_t>(card_index) >= tableau_[tableau_idx].size()) {
      return false;
    }
    
    // Check if the cards from this position to the bottom form a valid sequence
    return isValidTableauSequence(std::vector<cardlib::Card>(
      tableau_[tableau_idx].begin() + card_index, tableau_[tableau_idx].end()));
  }
  
  return false;
}

bool FreecellGame::checkWinCondition() const {
  if (current_game_mode_ == GameMode::CLASSIC_FREECELL) {
    // Classic FreeCell: Each foundation pile should have 13 cards (A-K)
    for (const auto &pile : foundation_) {
      if (pile.size() != 13) {
        return false;
      }
    }
  } else {
    // Double FreeCell: Each foundation pile should have 26 cards (two sets of A-K)
    for (const auto &pile : foundation_) {
      if (pile.size() != 26) {
        return false;
      }
    }
  }
  
  // Check if all other piles are empty
  for (const auto &cell : freecells_) {
    if (cell.has_value()) {
      return false;
    }
  }
  
  for (const auto &pile : tableau_) {
    if (!pile.empty()) {
      return false;
    }
  }
  
  return true;
}


// Tableau movement validation functions
bool FreecellGame::isValidTableauSequence(const std::vector<cardlib::Card>& cards) const {
  if (cards.size() <= 1) {
    return true;  // Single card or empty sequence is always valid
  }
  
  // Check that cards form a valid sequence (alternating colors, descending rank)
  for (size_t i = 0; i < cards.size() - 1; i++) {
    const cardlib::Card& upper_card = cards[i];
    const cardlib::Card& lower_card = cards[i + 1];
    
    // Cards must be in alternating colors
    bool different_colors = isCardRed(upper_card) != isCardRed(lower_card);
    
    // Cards must be in descending rank (upper card's rank = lower card's rank + 1)
    bool descending_rank = static_cast<int>(upper_card.rank) == static_cast<int>(lower_card.rank) + 1;
    
    if (!different_colors || !descending_rank) {
      return false;
    }
  }
  
  return true;
}

bool FreecellGame::isCardRed(const cardlib::Card& card) const {
  return card.suit == cardlib::Suit::HEARTS || card.suit == cardlib::Suit::DIAMONDS;
}

gboolean FreecellGame::onMotionNotify(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);

  if (game->dragging_) {
    game->drag_start_x_ = event->x;
    game->drag_start_y_ = event->y;
    gtk_widget_queue_draw(game->game_area_);
  }

  return TRUE;
}
