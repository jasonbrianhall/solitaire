#include "solitaire.h"
#include <gtk/gtk.h>

// Handler for keyboard events
gboolean SolitaireGame::onKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  
  // Check for control key modifier
  bool ctrl_pressed = (event->state & GDK_CONTROL_MASK);

  // If any animation is active, block all keyboard input except Escape
  if (game->foundation_move_animation_active_ || 
      game->stock_to_waste_animation_active_ ||
      game->deal_animation_active_ ||
      game->win_animation_active_) {
    if (event->keyval == GDK_KEY_Escape) {
      // Allow Escape to cancel animations
      return TRUE;
    }
    return FALSE;  // Ignore other keys during animations
  }
  
  switch (event->keyval) {
    case GDK_KEY_F11:
      game->toggleFullscreen();
      return TRUE;
      
    case GDK_KEY_Escape:
      if (game->is_fullscreen_ && !game->keyboard_selection_active_) {
        game->toggleFullscreen();
        return TRUE;
      }

      if (game->keyboard_selection_active_) {
        game->keyboard_selection_active_ = false;
        game->source_pile_ = -1;
        game->source_card_idx_ = -1;
        game->refreshDisplay();
        return TRUE;
      }
      break;
      
    case GDK_KEY_n:
    case GDK_KEY_N:
      if (ctrl_pressed) {
        game->initializeGame();
        game->refreshDisplay();
        return TRUE;
      }
      break;
      
    case GDK_KEY_q:
    case GDK_KEY_Q:
      if (ctrl_pressed) {
        gtk_main_quit();
        return TRUE;
      }
      break;
      
    case GDK_KEY_h:
    case GDK_KEY_H:
      if (ctrl_pressed) {
        onAbout(nullptr, game);
        return TRUE;
      }
      break;
      
    case GDK_KEY_1:
      if (game->draw_three_mode_) {
        game->draw_three_mode_ = false;
        game->refreshDisplay();
      }
      return TRUE;
      
    case GDK_KEY_3:
      if (!game->draw_three_mode_) {
        game->draw_three_mode_ = true;
        game->refreshDisplay();
      }
      return TRUE;

    case GDK_KEY_f:
    case GDK_KEY_F:
      // Auto-finish game by moving cards to foundation
      game->autoFinishGame();
      return TRUE;
      
    case GDK_KEY_space:
      // Draw cards from stock with spacebar
      game->handleStockPileClick();
      return TRUE;
      
    case GDK_KEY_F1:
      // F1 for Help/About (common standard)
      onAbout(nullptr, game);
      return TRUE;
      
    case GDK_KEY_Left:
      // Select the previous (left) pile
      game->keyboard_navigation_active_ = true;
      game->selectPreviousPile();
      return TRUE;
      
    case GDK_KEY_Right:
      // Select the next (right) pile
      game->keyboard_navigation_active_ = true;
      game->selectNextPile();
      return TRUE;
      
    case GDK_KEY_Up:
      // Move selection up in a tableau pile
      game->keyboard_navigation_active_ = true;
      game->selectCardUp();
      return TRUE;
      
    case GDK_KEY_Down:
      // Move selection down in a tableau pile
      game->keyboard_navigation_active_ = true;
      game->selectCardDown();
      return TRUE;
      
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      // Activate the currently selected card/pile
      game->activateSelected();
      return TRUE;
  }
  
  return FALSE; // Let other handlers process this key event
}

// Toggle fullscreen mode
void SolitaireGame::toggleFullscreen() {
  if (is_fullscreen_) {
    gtk_window_unfullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = false;
  } else {
    gtk_window_fullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = true;
  }
}

// Select the next (right) pile
void SolitaireGame::selectNextPile() {
  if (selected_pile_ == -1) {
    // Start with stock pile (index 0)
    selected_pile_ = 0;
    selected_card_idx_ = stock_.empty() ? -1 : 0;
  } else {
    // Move to the next pile
    selected_pile_++;
    
    // Wrap around if we're past the last tableau pile (index 12)
    if (selected_pile_ > 12) {
      selected_pile_ = 0;
    }
    
    // Set card index based on the selected pile
    if (selected_pile_ == 0) {
      selected_card_idx_ = stock_.empty() ? -1 : 0;
    } else if (selected_pile_ == 1) {
      selected_card_idx_ = waste_.empty() ? -1 : waste_.size() - 1;
    } else if (selected_pile_ >= 2 && selected_pile_ <= 5) {
      int foundation_idx = selected_pile_ - 2;
      selected_card_idx_ = foundation_[foundation_idx].empty() ? -1 : foundation_[foundation_idx].size() - 1;
    } else if (selected_pile_ >= 6 && selected_pile_ <= 12) {
      int tableau_idx = selected_pile_ - 6;
      selected_card_idx_ = tableau_[tableau_idx].empty() ? -1 : tableau_[tableau_idx].size() - 1;
    }
  }
  
  refreshDisplay();
}

// Select the previous (left) pile
void SolitaireGame::selectPreviousPile() {
  if (selected_pile_ == -1) {
    // Start with the last tableau pile (index 12)
    selected_pile_ = 12;
    selected_card_idx_ = tableau_[6].empty() ? -1 : tableau_[6].size() - 1;
  } else {
    // Move to the previous pile
    selected_pile_--;
    
    // Wrap around if we're before the stock pile (index 0)
    if (selected_pile_ < 0) {
      selected_pile_ = 12;
    }
    
    // Set card index based on the selected pile
    if (selected_pile_ == 0) {
      selected_card_idx_ = stock_.empty() ? -1 : 0;
    } else if (selected_pile_ == 1) {
      selected_card_idx_ = waste_.empty() ? -1 : waste_.size() - 1;
    } else if (selected_pile_ >= 2 && selected_pile_ <= 5) {
      int foundation_idx = selected_pile_ - 2;
      selected_card_idx_ = foundation_[foundation_idx].empty() ? -1 : foundation_[foundation_idx].size() - 1;
    } else if (selected_pile_ >= 6 && selected_pile_ <= 12) {
      int tableau_idx = selected_pile_ - 6;
      selected_card_idx_ = tableau_[tableau_idx].empty() ? -1 : tableau_[tableau_idx].size() - 1;
    }
  }
  
  refreshDisplay();
}

// Move selection up in a tableau pile
void SolitaireGame::selectCardUp() {
  if (selected_pile_ < 6 || selected_pile_ > 12) {
    return;
  }
  
  int tableau_idx = selected_pile_ - 6;
  if (tableau_idx < 0 || tableau_idx >= tableau_.size()) {
    return;
  }
  
  // Only proceed if the tableau pile has cards
  if (tableau_[tableau_idx].empty()) {
    return;
  }
  
  // Ensure selected_card_idx_ is valid
  if (selected_card_idx_ < 0 || 
      selected_card_idx_ >= static_cast<int>(tableau_[tableau_idx].size())) {
    // Reset to a valid state
    selected_card_idx_ = tableau_[tableau_idx].size() - 1;
    return;
  }

  if (selected_pile_ >= 6 && selected_pile_ <= 12) {
    int tableau_idx = selected_pile_ - 6;
    
    // If we're looking at a tableau pile
    if (!tableau_[tableau_idx].empty() && selected_card_idx_ > 0) {
      // Try to navigate up within the tableau pile
      for (int i = selected_card_idx_ - 1; i >= 0; i--) {
        if (tableau_[tableau_idx][i].face_up) {
          selected_card_idx_ = i;
          refreshDisplay();
          return;
        }
      }
    }
    
    // If we couldn't navigate up or we're at the top card,
    // move to the corresponding pile in the top row
    if (tableau_idx == 0) {
      // Pile 0 goes to stock pile
      selected_pile_ = 0;
      selected_card_idx_ = stock_.empty() ? -1 : 0;
    } else if (tableau_idx == 1) {
      // Pile 1 goes to waste pile
      selected_pile_ = 1;
      selected_card_idx_ = waste_.empty() ? -1 : waste_.size() - 1;
    } else if (tableau_idx >= 2) {
      // Other piles go to corresponding foundation (if available) or empty space
      int foundation_idx = tableau_idx - 2;
      if (foundation_idx < foundation_.size()) {
        selected_pile_ = 2 + foundation_idx;
        selected_card_idx_ = foundation_[foundation_idx].empty() ? -1 : foundation_[foundation_idx].size() - 1;
      }
    }
  }
  
  refreshDisplay();
}


// Move selection down in a tableau pile
void SolitaireGame::selectCardDown() {
  // If we're in the top row, move down to the corresponding tableau pile
  if (selected_pile_ >= 0 && selected_pile_ <= 5) {
    int target_tableau;
    
    if (selected_pile_ == 0) {
      // Stock pile goes to tableau pile 0
      target_tableau = 0;
    } else if (selected_pile_ == 1) {
      // Waste pile goes to tableau pile 1
      target_tableau = 1;
    } else {
      // Foundation piles go to corresponding tableau piles (2-5 -> 2-5)
      target_tableau = selected_pile_;
    }
    
    // Check if the target_tableau is within range
    if (target_tableau < tableau_.size()) {
      selected_pile_ = 6 + target_tableau;
      selected_card_idx_ = tableau_[target_tableau].empty() ? -1 : tableau_[target_tableau].size() - 1;
    }
  }
  // If we're already in the tableau, try to move down
  else if (selected_pile_ >= 6 && selected_pile_ <= 12) {
    int tableau_idx = selected_pile_ - 6;
    if (!tableau_[tableau_idx].empty() && selected_card_idx_ >= 0) {
      if (static_cast<size_t>(selected_card_idx_) < tableau_[tableau_idx].size() - 1) {
        selected_card_idx_++;
      }
    }
  }
  
  refreshDisplay();
}


// Activate the currently selected card/pile (like clicking)
void SolitaireGame::activateSelected() {
  if (selected_pile_ == -1) {
    return;
  }
  
  // If any animation is active, don't allow selection or moves
  if (foundation_move_animation_active_ || 
      stock_to_waste_animation_active_ ||
      deal_animation_active_ ||
      win_animation_active_) {
    return;
  }
  
  // If we already have a selection active, try to move to the current pile
  if (keyboard_selection_active_) {
    // We have a source card selected, try to move it to the current pile
    if (tryMoveSelectedCard()) {
      // Move was successful, deactivate selection
      keyboard_selection_active_ = false;
      source_pile_ = -1;
      source_card_idx_ = -1;
    }
    // Always refresh display whether move succeeded or not
    refreshDisplay();
    return;
  }
  
  // Handle stock pile (draw cards)
  if (selected_pile_ == 0) {
    handleStockPileClick();
    return;
  }
  
  // Try to move to foundation first
  if (selected_pile_ == 1 || (selected_pile_ >= 6 && selected_pile_ <= 12)) {
    const cardlib::Card *card = nullptr;
    int target_foundation = -1;
    
    // Get the card based on pile type
    if (selected_pile_ == 1 && !waste_.empty()) {
      card = &waste_.back();
      
      // Find which foundation to move to
      for (int i = 0; i < foundation_.size(); i++) {
        if (canMoveToFoundation(*card, i)) {
          target_foundation = i;
          break;
        }
      }
      
      if (target_foundation >= 0) {
        // Start animation
        startFoundationMoveAnimation(*card, selected_pile_, 0, target_foundation + 2);
        
        // Remove card from waste pile
        waste_.pop_back();
        
        // Update selection to point to new top card
        selected_card_idx_ = waste_.empty() ? -1 : waste_.size() - 1;
        return;
      }
    } else if (selected_pile_ >= 6 && selected_pile_ <= 12) {
      int tableau_idx = selected_pile_ - 6;
      auto &tableau_pile = tableau_[tableau_idx];
      
      if (!tableau_pile.empty() && selected_card_idx_ >= 0 && 
          tableau_pile[selected_card_idx_].face_up) {
        
        card = &tableau_pile[selected_card_idx_].card;
        
        // Find which foundation to move to
        for (int i = 0; i < foundation_.size(); i++) {
          if (canMoveToFoundation(*card, i)) {
            target_foundation = i;
            break;
          }
        }
        
        if (target_foundation >= 0) {
          // Check if we're moving the top card
          if (static_cast<size_t>(selected_card_idx_) == tableau_pile.size() - 1) {
            // Start animation
            startFoundationMoveAnimation(*card, selected_pile_, selected_card_idx_, target_foundation + 2);
            
            // Remove card from tableau
            tableau_pile.pop_back();
            
            // Flip new top card if needed
            if (!tableau_pile.empty() && !tableau_pile.back().face_up) {
              tableau_pile.back().face_up = true;
            }
            
            // Update selection to point to new top card
            selected_card_idx_ = tableau_pile.empty() ? -1 : tableau_pile.size() - 1;
            return;
          }
        }
      }
    }
  }
  
  // If we get here, we couldn't move to foundation
  // For tableau piles, activate selection for moving between piles
  if (selected_pile_ >= 6 && selected_pile_ <= 12) {
    int tableau_idx = selected_pile_ - 6;
    auto &tableau_pile = tableau_[tableau_idx];
    
    if (!tableau_pile.empty() && selected_card_idx_ >= 0 && 
        tableau_pile[selected_card_idx_].face_up) {
      // Activate selection for this card
      keyboard_selection_active_ = true;
      source_pile_ = selected_pile_;
      source_card_idx_ = selected_card_idx_;
      // Force a refresh immediately to show the blue highlight
      refreshDisplay();
    }
  }
  else if (selected_pile_ == 1 && !waste_.empty()) {
    // Allow selecting waste pile top card
    keyboard_selection_active_ = true;
    source_pile_ = selected_pile_;
    source_card_idx_ = waste_.size() - 1;
    // Force a refresh immediately to show the blue highlight
    refreshDisplay();
  }
}

bool SolitaireGame::tryMoveSelectedCard() {
  if (source_pile_ == -1 || selected_pile_ == -1) {
    return false;
  }
  
  // Validate source_pile_ and source_card_idx_
  if (source_pile_ == 1) {
    if (waste_.empty() || source_card_idx_ < 0 || 
        source_card_idx_ >= static_cast<int>(waste_.size())) {
      return false;
    }
  } else if (source_pile_ >= 6 && source_pile_ <= 12) {
    int tableau_idx = source_pile_ - 6;
    if (tableau_idx < 0 || tableau_idx >= tableau_.size() ||
        tableau_[tableau_idx].empty() || source_card_idx_ < 0 ||
        source_card_idx_ >= static_cast<int>(tableau_[tableau_idx].size())) {
      return false;
    }
  } else {
    return false;
  }
  
  // Don't allow moving to the same pile
  if (source_pile_ == selected_pile_) {
    return false;
  }
  
  std::vector<cardlib::Card> source_cards;
  std::vector<cardlib::Card> target_cards;
  
  // Get source cards
  if (source_pile_ == 1) {
    // Waste pile - can only move top card
    if (!waste_.empty()) {
      source_cards = {waste_.back()};
    }
  } 
  else if (source_pile_ >= 6 && source_pile_ <= 12) {
    // Tableau pile - can move the selected card and all cards below it
    int tableau_idx = source_pile_ - 6;
    auto &source_tableau = tableau_[tableau_idx];
    
    if (!source_tableau.empty() && source_card_idx_ >= 0 && 
        static_cast<size_t>(source_card_idx_) < source_tableau.size() && 
        source_tableau[source_card_idx_].face_up) {
      
      for (size_t i = source_card_idx_; i < source_tableau.size(); i++) {
        source_cards.push_back(source_tableau[i].card);
      }
    }
  }
  
  // Get target cards
  if (selected_pile_ >= 2 && selected_pile_ <= 5) {
    // Foundation pile
    int foundation_idx = selected_pile_ - 2;
    target_cards = foundation_[foundation_idx];
  } 
  else if (selected_pile_ >= 6 && selected_pile_ <= 12) {
    // Tableau pile
    int tableau_idx = selected_pile_ - 6;
    auto &target_tableau = tableau_[tableau_idx];
    
    if (!target_tableau.empty()) {
      target_cards = {target_tableau.back().card};
    }
    // Empty pile case - leave target_cards empty
  }
  
  // Check if the move is valid
  bool is_foundation = (selected_pile_ >= 2 && selected_pile_ <= 5);
  if (source_cards.empty() || !canMoveToPile(source_cards, target_cards, is_foundation)) {
    return false;
  }
  
  // Execute the move
  if (source_pile_ == 1) {
    // Moving from waste pile
    // Remove card from waste
    waste_.pop_back();
    
    if (is_foundation) {
      // Add to foundation
      foundation_[selected_pile_ - 2].push_back(source_cards[0]);
    } else {
      // Add to tableau
      int tableau_idx = selected_pile_ - 6;
      tableau_[tableau_idx].emplace_back(source_cards[0], true);
    }
  } 
  else if (source_pile_ >= 6 && source_pile_ <= 12) {
    // Moving from tableau pile
    int source_tableau_idx = source_pile_ - 6;
    auto &source_tableau = tableau_[source_tableau_idx];
    
    // Store cards to move
    std::vector<TableauCard> cards_to_move;
    for (size_t i = source_card_idx_; i < source_tableau.size(); i++) {
      cards_to_move.push_back(source_tableau[i]);
    }
    
    // Remove cards from source tableau
    source_tableau.erase(source_tableau.begin() + source_card_idx_, source_tableau.end());
    
    // Flip the new top card if needed
    if (!source_tableau.empty() && !source_tableau.back().face_up) {
      source_tableau.back().face_up = true;
    }
    
    if (is_foundation) {
      // Add to foundation (should be only one card)
      foundation_[selected_pile_ - 2].push_back(source_cards[0]);
    } else {
      // Add to target tableau
      int target_tableau_idx = selected_pile_ - 6;
      tableau_[target_tableau_idx].insert(
          tableau_[target_tableau_idx].end(), 
          cards_to_move.begin(), 
          cards_to_move.end());
    }
  }
  
  // Update selected card index after move
  if (selected_pile_ >= 6 && selected_pile_ <= 12) {
    int tableau_idx = selected_pile_ - 6;
    selected_card_idx_ = tableau_[tableau_idx].empty() ? -1 : tableau_[tableau_idx].size() - 1;
  }
  
  // Check for win
  /*if (checkWinCondition()) {
    startWinAnimation();
  }*/
  
  refreshDisplay();
  return true;
}

// Highlight the selected card in the onDraw method
void SolitaireGame::highlightSelectedCard(cairo_t *cr) {
  int x = 0, y = 0;


  if (!cr || selected_pile_ == -1) {
    return;
  }
  
  // Validate keyboard selection
  if (keyboard_selection_active_) {
    if (source_pile_ < 0 || 
        (source_pile_ >= 6 && source_pile_ - 6 >= tableau_.size()) ||
        (source_pile_ == 1 && waste_.empty())) {
      // Invalid source pile, reset selection state
      keyboard_selection_active_ = false;
      source_pile_ = -1;
      source_card_idx_ = -1;
      return;
    }
  }
  
  
  // Determine position based on pile type
  if (selected_pile_ == 0) {
    // Stock pile
    x = current_card_spacing_;
    y = current_card_spacing_;
  } else if (selected_pile_ == 1) {
    // Waste pile
    x = 2 * current_card_spacing_ + current_card_width_;
    y = current_card_spacing_;
  } else if (selected_pile_ >= 2 && selected_pile_ <= 5) {
    // Foundation piles
    x = (3 + selected_pile_ - 2) * (current_card_width_ + current_card_spacing_);
    y = current_card_spacing_;
  } else if (selected_pile_ >= 6 && selected_pile_ <= 12) {
    // Tableau piles
    int tableau_idx = selected_pile_ - 6;
    x = current_card_spacing_ + tableau_idx * (current_card_width_ + current_card_spacing_);
    
    // For empty tableau piles, highlight the empty space
    if (selected_card_idx_ == -1) {
      y = current_card_spacing_ + current_card_height_ + current_vert_spacing_;
    } else {
      y = current_card_spacing_ + current_card_height_ + current_vert_spacing_ + 
          selected_card_idx_ * current_vert_spacing_;
    }
  }
  
  // Choose highlight color based on whether we're selecting a card to move
  if (keyboard_selection_active_ && source_pile_ == selected_pile_ && 
      (source_card_idx_ == selected_card_idx_ || selected_card_idx_ == -1)) {
    // Source card/pile is highlighted in blue
    cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.5); // Semi-transparent blue
  } else {
    // Regular selection is highlighted in yellow
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.5); // Semi-transparent yellow
  }
  
  cairo_set_line_width(cr, 3.0);
  cairo_rectangle(cr, x - 2, y - 2, current_card_width_ + 4, current_card_height_ + 4);
  cairo_stroke(cr);
  
  // If we have a card selected for movement, highlight all cards below it in a tableau pile
  if (keyboard_selection_active_ && source_pile_ >= 6 && source_pile_ <= 12 && source_card_idx_ >= 0) {
    int tableau_idx = source_pile_ - 6;
    auto &tableau_pile = tableau_[tableau_idx];
    
    if (!tableau_pile.empty() && source_card_idx_ < tableau_pile.size()) {
      // Highlight all cards from the selected one to the bottom
      cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.3); // Lighter blue for stack
      
      x = current_card_spacing_ + tableau_idx * (current_card_width_ + current_card_spacing_);
      y = current_card_spacing_ + current_card_height_ + current_vert_spacing_ + 
          source_card_idx_ * current_vert_spacing_;
      
      // Draw a single rectangle that covers all cards in the stack
      int stack_height = (tableau_pile.size() - source_card_idx_ - 1) * current_vert_spacing_ + 
                        current_card_height_;
      
      if (stack_height > 0) {
        cairo_rectangle(cr, x - 2, y - 2, current_card_width_ + 4, stack_height + 4);
        cairo_stroke(cr);
      }
    }
  }
}

void SolitaireGame::resetKeyboardNavigation() {
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;
  source_pile_ = -1;
  source_card_idx_ = -1;
  selected_pile_ = -1;
  selected_card_idx_ = -1;
}
