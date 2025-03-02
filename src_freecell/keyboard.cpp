#include "freecell.h"
#include <gtk/gtk.h>

gboolean FreecellGame::onKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);

  // If win animation is active, stop it
  if (game->win_animation_active_) {
    game->stopWinAnimation();
    return TRUE;
  }

  // Check for control key modifier
  bool ctrl_pressed = (event->state & GDK_CONTROL_MASK);

  // If deal animation is active, block keyboard input except Escape
  if (game->deal_animation_active_) {
    if (event->keyval == GDK_KEY_Escape) {
      // Allow Escape to cancel animations
      game->completeDeal();
      return TRUE;
    }
    return FALSE; // Ignore other keys during animations
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

  case GDK_KEY_s:
  case GDK_KEY_S:
    if (ctrl_pressed) {
      game->sound_enabled_ = !game->sound_enabled_;

      // Find and update the sound menu item
      GList *menu_items = gtk_container_get_children(GTK_CONTAINER(game->vbox_));
      if (menu_items) {
        GtkWidget *menubar = GTK_WIDGET(menu_items->data);
        GList *menus = gtk_container_get_children(GTK_CONTAINER(menubar));
        if (menus) {
          // First menu should be the Game menu
          GtkWidget *game_menu_item = GTK_WIDGET(menus->data);
          GtkWidget *game_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(game_menu_item));
          if (game_menu) {
            GList *game_menu_items = gtk_container_get_children(GTK_CONTAINER(game_menu));
            // Find the sound checkbox
            for (GList *item = game_menu_items; item != NULL; item = item->next) {
              if (GTK_IS_CHECK_MENU_ITEM(item->data)) {
                GtkWidget *check_item = GTK_WIDGET(item->data);
                const gchar *label = gtk_menu_item_get_label(GTK_MENU_ITEM(check_item));
                if (label && strstr(label, "Sound") != NULL) {
                  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(check_item), game->sound_enabled_);
                  break;
                }
              }
            }
            g_list_free(game_menu_items);
          }
          g_list_free(menus);
        }
        g_list_free(menu_items);
      }
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

  case GDK_KEY_F1:
    // F1 for Help/About (common standard)
    onAbout(nullptr, game);
    return TRUE;

  case GDK_KEY_Left:
    // Select the previous (left) pile
    game->keyboard_navigation_active_ = true;
    game->selectPreviousPile();
    return TRUE;

  case GDK_KEY_f:
  case GDK_KEY_F:
    // Auto-finish game by moving cards to foundation
    // Don't do anything if an animation is already active
    if (!game->foundation_move_animation_active_ && !game->win_animation_active_) {
      game->autoFinishGame();
    }
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

  case GDK_KEY_space:
    // Spacebar behaves like right-click - try to auto-move cards
    if (game->keyboard_navigation_active_ && !game->keyboard_selection_active_) {
      if (game->handleSpacebarAction()) {
        return TRUE;
      }
    }
    break;

case GDK_KEY_l:
case GDK_KEY_L:
  if (ctrl_pressed) {
    // Show deck loading dialog
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Load Custom Card Deck", GTK_WINDOW(game->window_),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);
    
    // Create file filter for ZIP files
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Card Deck Files (*.zip)");
    gtk_file_filter_add_pattern(filter, "*.zip");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      char *filename =
          gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
      
      try {
        // Try to load the new deck
        game->deck_ = cardlib::Deck(filename);
        game->deck_.removeJokers();
        game->deck_.shuffle(game->current_seed_);
        
        // Reinitialize card cache with new deck
        game->initializeCardCache();
        
        // Restart the game with the new deck
        game->initializeGame();
        game->refreshDisplay();
        
        // Optional: Show success message
        GtkWidget *success_dialog = gtk_message_dialog_new(
            GTK_WINDOW(game->window_), GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "Custom deck loaded successfully!");
        gtk_dialog_run(GTK_DIALOG(success_dialog));
        gtk_widget_destroy(success_dialog);
        
      } catch (const std::exception &e) {
        // Show error message if deck loading fails
        GtkWidget *error_dialog = gtk_message_dialog_new(
            GTK_WINDOW(game->window_), GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Failed to load deck: %s", e.what());
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
      }
      
      g_free(filename);
    }
    gtk_widget_destroy(dialog);
    return TRUE;
  }
  break;

  case GDK_KEY_r:
  case GDK_KEY_R:
    if (ctrl_pressed) {
      // Restart the current game with the same seed
      game->restartGame();
      return TRUE;
    }
    break;

  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:
    // Activate the currently selected card/pile
    game->activateSelected();
    return TRUE;
  }

  return FALSE; // Let other handlers process this key event
}

int FreecellGame::findFirstPlayableCard(int tableau_idx) {
  if (tableau_idx < 0 || static_cast<size_t>(tableau_idx) >= tableau_.size() || tableau_[tableau_idx].empty()) {
    return -1;
  }
  
  // In Freecell, the first playable card is the one that starts a valid sequence
  // going to the bottom of the pile
  int candidate = -1;
  
  // Start from bottom and work upward
  for (int i = tableau_[tableau_idx].size() - 1; i >= 0; i--) {
    // If this is the bottom card, it's always playable
    if (i == static_cast<int>(tableau_[tableau_idx].size() - 1)) {
      candidate = i;
      continue;
    }
    
    // Check if this card and the one below it form a valid sequence
    const cardlib::Card& current_card = tableau_[tableau_idx][i];
    const cardlib::Card& next_card = tableau_[tableau_idx][i + 1];
    
    bool different_colors = isCardRed(current_card) != isCardRed(next_card);
    bool descending_rank = static_cast<int>(current_card.rank) == static_cast<int>(next_card.rank) + 1;
    
    // If they form a valid sequence, this card could be a candidate
    if (different_colors && descending_rank) {
      candidate = i;
    } else {
      // We found a break in valid sequences
      break;
    }
  }
  
  return candidate;
}

// Select the next (right) pile
void FreecellGame::selectNextPile() {
  if (selected_pile_ == -1) {
    // Start with freecell (index 0)
    selected_pile_ = 0;
    selected_card_idx_ = 0;
  } else {
    // Move to the next pile
    selected_pile_++;

    // Wrap around if we're past the last tableau pile (index 15)
    // In Freecell: 0-3 are freecells, 4-7 are foundation, 8-15 are tableau
    if (selected_pile_ > 15) {
      selected_pile_ = 0;
    }

    // Set card index based on the selected pile
    if (selected_pile_ >= 0 && selected_pile_ <= 3) {
      // Freecells - only one card possible
      selected_card_idx_ = 0;
    } else if (selected_pile_ >= 4 && selected_pile_ <= 7) {
      // Foundation piles - select top card
      int foundation_idx = selected_pile_ - 4;
      selected_card_idx_ = foundation_[foundation_idx].empty() ? 
                          -1 : foundation_[foundation_idx].size() - 1;
    } else if (selected_pile_ >= 8 && selected_pile_ <= 15) {
      // Tableau piles - select bottom (visible) card
      int tableau_idx = selected_pile_ - 8;
      selected_card_idx_ = tableau_[tableau_idx].empty() ? 
                          -1 : tableau_[tableau_idx].size() - 1;
    }
  }

  refreshDisplay();
}

// Select the previous (left) pile
void FreecellGame::selectPreviousPile() {
  if (selected_pile_ == -1) {
    // Start with the last tableau pile (index 15)
    selected_pile_ = 15;
    selected_card_idx_ = tableau_[7].empty() ? -1 : tableau_[7].size() - 1;
  } else {
    // Move to the previous pile
    selected_pile_--;

    // Wrap around if we're before the first freecell (index 0)
    if (selected_pile_ < 0) {
      selected_pile_ = 15;
    }

    // Set card index based on the selected pile
    if (selected_pile_ >= 0 && selected_pile_ <= 3) {
      // Freecells - only one card possible
      selected_card_idx_ = 0;
    } else if (selected_pile_ >= 4 && selected_pile_ <= 7) {
      // Foundation piles - select top card
      int foundation_idx = selected_pile_ - 4;
      selected_card_idx_ = foundation_[foundation_idx].empty() ? 
                          -1 : foundation_[foundation_idx].size() - 1;
    } else if (selected_pile_ >= 8 && selected_pile_ <= 15) {
      // Tableau piles - select bottom (visible) card
      int tableau_idx = selected_pile_ - 8;
      selected_card_idx_ = tableau_[tableau_idx].empty() ? 
                          -1 : tableau_[tableau_idx].size() - 1;
    }
  }

  refreshDisplay();
}

// Move selection up in a tableau pile
void FreecellGame::selectCardUp() {
  if (selected_pile_ < 8 || selected_pile_ > 15) {
    return;
  }

  int tableau_idx = selected_pile_ - 8;
  if (tableau_idx < 0 || static_cast<size_t>(tableau_idx) >= tableau_.size()) {
    return;
  }

  // If the tableau pile is empty, no action
  if (tableau_[tableau_idx].empty()) {
    return;
  }
  
  // If we're at the top playable card already, move to freecell or foundation
  int first_playable = findFirstPlayableCard(tableau_idx);
  
  if (selected_card_idx_ == first_playable || first_playable == -1) {
    // For the first 4 tableau piles, go to corresponding freecell
    if (tableau_idx < 4) {
      selected_pile_ = tableau_idx; // Freecell index matches first 4 tableau
    }
    // For the last 4 tableau piles, go to corresponding foundation
    else if (tableau_idx >= 4 && tableau_idx < 8) {
      selected_pile_ = tableau_idx; // Foundation index matches with last 4 tableau
    }
    refreshDisplay();
    return;
  }

  // Find the next valid playable card above the current one
  int next_playable = -1;
  for (int i = selected_card_idx_ - 1; i >= first_playable; i--) {
    // Check if cards from i to the bottom form a valid sequence
    bool valid_sequence = true;
    for (size_t j = i; j < tableau_[tableau_idx].size() - 1; j++) {
      const cardlib::Card& card1 = tableau_[tableau_idx][j];
      const cardlib::Card& card2 = tableau_[tableau_idx][j + 1];
      
      bool different_colors = isCardRed(card1) != isCardRed(card2);
      bool descending_rank = static_cast<int>(card1.rank) == static_cast<int>(card2.rank) + 1;
      
      if (!different_colors || !descending_rank) {
        valid_sequence = false;
        break;
      }
    }
    
    if (valid_sequence) {
      next_playable = i;
      break;
    }
  }
  
  if (next_playable != -1) {
    selected_card_idx_ = next_playable;
    refreshDisplay();
  }
}


// Move selection down in a tableau pile
void FreecellGame::selectCardDown() {
  // If we're in a freecell or foundation, move down to corresponding tableau
  if (selected_pile_ >= 0 && selected_pile_ <= 7) {
    // Map the freecell/foundation index to tableau index
    selected_pile_ = selected_pile_ + 8;
    
    // In tableau piles - select top playable card
    int tableau_idx = selected_pile_ - 8;
    int first_playable = findFirstPlayableCard(tableau_idx);
    
    if (first_playable != -1) {
      selected_card_idx_ = first_playable;
    } else {
      selected_card_idx_ = tableau_[tableau_idx].empty() ? -1 : 0;
    }
  }
  // If we're already in the tableau, move down to next playable card or sequence
  else if (selected_pile_ >= 8 && selected_pile_ <= 15) {
    int tableau_idx = selected_pile_ - 8;
    if (!tableau_[tableau_idx].empty() && selected_card_idx_ >= 0) {
      // Find the next playable card/sequence below the current one
      int next_card = -1;
      
      // We'll only go down if we're in a valid sequence of cards that can be moved together
      bool valid_sequence = true;
      for (size_t i = selected_card_idx_; i < tableau_[tableau_idx].size() - 1; i++) {
        const cardlib::Card& card1 = tableau_[tableau_idx][i];
        const cardlib::Card& card2 = tableau_[tableau_idx][i + 1];
        
        bool different_colors = isCardRed(card1) != isCardRed(card2);
        bool descending_rank = static_cast<int>(card1.rank) == static_cast<int>(card2.rank) + 1;
        
        if (!different_colors || !descending_rank) {
          valid_sequence = false;
          break;
        }
        
        // This is a candidate for the next card
        next_card = i + 1;
      }
      
      if (valid_sequence && next_card != -1 && 
          static_cast<size_t>(selected_card_idx_) < tableau_[tableau_idx].size() - 1) {
        selected_card_idx_ = next_card;
        refreshDisplay();
      }
    }
  }

  refreshDisplay();
}

// Activate the currently selected card/pile
void FreecellGame::activateSelected() {
  if (selected_pile_ == -1) {
    return;
  }

  // If deal animation is active, don't allow selection or moves
  if (deal_animation_active_) {
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
      
      // Play card movement sound
      playSound(GameSoundEvent::CardPlace);
    } else {
      // Move failed, play error sound (optional)
      // playSound(GameSoundEvent::Error); // If you have an error sound
    }
    // Always refresh display whether move succeeded or not
    refreshDisplay();
    return;
  }

  // Select a card for moving - only if it's playable
  if (canSelectForMove() && isCardPlayable()) {
    keyboard_selection_active_ = true;
    source_pile_ = selected_pile_;
    source_card_idx_ = selected_card_idx_;
    
    refreshDisplay();
  }
}


// Check if the current selection can be selected for a move
bool FreecellGame::canSelectForMove() {
  // Validate pile indices
  if (selected_pile_ < 0) {
    return false;
  }
  
  // Freecells (0-3)
  if (selected_pile_ <= 3) {
    // Can select if the freecell has a card
    return freecells_[selected_pile_].has_value();
  }
  // Foundation piles (4-7)
  else if (selected_pile_ <= 7) {
    int foundation_idx = selected_pile_ - 4;
    // Can select if the foundation pile has at least one card
    return !foundation_[foundation_idx].empty();
  }
  // Tableau piles (8-15)
  else if (selected_pile_ <= 15) {
    int tableau_idx = selected_pile_ - 8;
    // Check if tableau index is valid and the pile has cards
    if (tableau_idx < 0 || tableau_idx >= tableau_.size() || tableau_[tableau_idx].empty()) {
      return false;
    }
    
    // Can select card if it's in the tableau
    return selected_card_idx_ >= 0 && selected_card_idx_ < tableau_[tableau_idx].size();
  }
  
  return false;
}

// Try to move the selected card to the current destination
bool FreecellGame::tryMoveSelectedCard() {
  // Invalid selection state
  if (source_pile_ < 0 || selected_pile_ < 0 || source_pile_ == selected_pile_) {
    return false;
  }
  
  // Source: Freecell (0-3)
  if (source_pile_ <= 3) {
    return tryMoveFromFreecell();
  }
  // Source: Foundation (4-7)
  else if (source_pile_ <= 7) {
    return tryMoveFromFoundation();
  }
  // Source: Tableau (8-15)
  else if (source_pile_ <= 15) {
    return tryMoveFromTableau();
  }
  
  return false;
}

// Try to move a card from a freecell
bool FreecellGame::tryMoveFromFreecell() {
  // Check source is valid
  if (source_pile_ < 0 || source_pile_ > 3 || !freecells_[source_pile_].has_value()) {
    return false;
  }
  
  cardlib::Card card_to_move = freecells_[source_pile_].value();
  
  // Destination: Freecell (0-3)
  if (selected_pile_ <= 3) {
    // Check if destination freecell is empty
    if (!freecells_[selected_pile_].has_value()) {
      // Move card to destination freecell
      freecells_[selected_pile_] = card_to_move;
      // Clear source freecell
      freecells_[source_pile_] = std::nullopt;
      return true;
    }
  }
  // Destination: Foundation (4-7) 
  else if (selected_pile_ <= 7) {
    int foundation_idx = selected_pile_ - 4;
    // Check if card can be moved to foundation
    if (canMoveToFoundation(card_to_move, foundation_idx)) {
      // Add to foundation
      foundation_[foundation_idx].push_back(card_to_move);
      // Clear source freecell
      freecells_[source_pile_] = std::nullopt;
      return true;
    }
  }
  // Destination: Tableau (8-15)
  else if (selected_pile_ <= 15) {
    int tableau_idx = selected_pile_ - 8;
    // Check if card can be moved to tableau
    if (canMoveToTableau(card_to_move, tableau_idx)) {
      // Add to tableau
      tableau_[tableau_idx].push_back(card_to_move);
      // Clear source freecell
      freecells_[source_pile_] = std::nullopt;
      return true;
    }
  }
  
  return false;
}

// Try to move a card from a foundation pile
bool FreecellGame::tryMoveFromFoundation() {
  int foundation_idx = source_pile_ - 4;
  
  // Check source is valid
  if (foundation_idx < 0 || foundation_idx >= foundation_.size() || foundation_[foundation_idx].empty()) {
    return false;
  }
  
  cardlib::Card card_to_move = foundation_[foundation_idx].back();
  
  // Destination: Freecell (0-3)
  if (selected_pile_ <= 3) {
    // Check if destination freecell is empty
    if (!freecells_[selected_pile_].has_value()) {
      // Move card to destination freecell
      freecells_[selected_pile_] = card_to_move;
      // Remove from foundation
      foundation_[foundation_idx].pop_back();
      return true;
    }
  }
  // Destination: Tableau (8-15)
  else if (selected_pile_ <= 15) {
    int tableau_idx = selected_pile_ - 8;
    // Check if card can be moved to tableau
    if (canMoveToTableau(card_to_move, tableau_idx)) {
      // Add to tableau
      tableau_[tableau_idx].push_back(card_to_move);
      // Remove from foundation
      foundation_[foundation_idx].pop_back();
      return true;
    }
  }
  
  return false;
}

// Try to move cards from a tableau pile
bool FreecellGame::tryMoveFromTableau() {
  int tableau_idx = source_pile_ - 8;
  
  // Check source is valid
  if (tableau_idx < 0 || tableau_idx >= tableau_.size() || tableau_[tableau_idx].empty() ||
      source_card_idx_ < 0 || source_card_idx_ >= tableau_[tableau_idx].size()) {
    return false;
  }
  
  // For tableau, we need to determine how many cards we're moving
  std::vector<cardlib::Card> cards_to_move;
  for (size_t i = source_card_idx_; i < tableau_[tableau_idx].size(); i++) {
    cards_to_move.push_back(tableau_[tableau_idx][i]);
  }
  
  if (cards_to_move.empty()) {
    return false;
  }
  
  // Single card move - could go to freecell, foundation, or tableau
  if (cards_to_move.size() == 1) {
    cardlib::Card card = cards_to_move[0];
    
    // Destination: Freecell (0-3)
    if (selected_pile_ <= 3) {
      // Check if destination freecell is empty
      if (!freecells_[selected_pile_].has_value()) {
        // Move card to destination freecell
        freecells_[selected_pile_] = card;
        // Remove from tableau
        tableau_[tableau_idx].pop_back();
        return true;
      }
    }
    // Destination: Foundation (4-7)
    else if (selected_pile_ <= 7) {
      int foundation_idx = selected_pile_ - 4;
      // Check if card can be moved to foundation
      if (canMoveToFoundation(card, foundation_idx)) {
        // Add to foundation
        foundation_[foundation_idx].push_back(card);
        // Remove from tableau
        tableau_[tableau_idx].pop_back();
        return true;
      }
    }
    // Destination: Tableau (8-15)
    else if (selected_pile_ <= 15) {
      int dest_tableau_idx = selected_pile_ - 8;
      // Can't move to same tableau
      if (dest_tableau_idx == tableau_idx) {
        return false;
      }
      // Check if card can be moved to tableau
      if (canMoveToTableau(card, dest_tableau_idx)) {
        // Add to destination tableau
        tableau_[dest_tableau_idx].push_back(card);
        // Remove from source tableau
        tableau_[tableau_idx].pop_back();
        return true;
      }
    }
  }
  // Multi-card move - can only go to another tableau
  else {
    // Destination: Tableau (8-15)
    if (selected_pile_ <= 15) {
      int dest_tableau_idx = selected_pile_ - 8;
      // Can't move to same tableau
      if (dest_tableau_idx == tableau_idx) {
        return false;
      }
      
      // Check if the move is valid
      if (canMoveTableauStack(cards_to_move, dest_tableau_idx)) {
        // Add all cards to destination tableau
        for (const auto& card : cards_to_move) {
          tableau_[dest_tableau_idx].push_back(card);
        }
        
        // Remove cards from source tableau
        tableau_[tableau_idx].erase(
          tableau_[tableau_idx].begin() + source_card_idx_,
          tableau_[tableau_idx].end()
        );
        
        return true;
      }
    }
  }
  
  return false;
}

// Check if a card can be moved to a foundation pile
bool FreecellGame::canMoveToFoundation(const cardlib::Card& card, int foundation_idx) {
  // Foundation must be within range
  if (foundation_idx < 0 || foundation_idx >= foundation_.size()) {
    return false;
  }
  
  // Empty foundation can only accept Ace
  if (foundation_[foundation_idx].empty()) {
    return card.rank == cardlib::Rank::ACE;
  }
  
  // Non-empty foundation - check suit and rank
  const cardlib::Card& top_card = foundation_[foundation_idx].back();
  return (card.suit == top_card.suit && 
         static_cast<int>(card.rank) == static_cast<int>(top_card.rank) + 1);
}

// Check if a card can be moved to a tableau pile
bool FreecellGame::canMoveToTableau(const cardlib::Card& card, int tableau_idx) {
  // Tableau must be within range
  if (tableau_idx < 0 || tableau_idx >= tableau_.size()) {
    return false;
  }
  
  // Empty tableau can accept any card
  if (tableau_[tableau_idx].empty()) {
    return true;
  }
  
  // Non-empty tableau - check color and rank
  const cardlib::Card& top_card = tableau_[tableau_idx].back();
  
  // Cards must be in alternating colors and descending rank
  bool different_colors = isCardRed(card) != isCardRed(top_card);
  bool descending_rank = static_cast<int>(card.rank) + 1 == static_cast<int>(top_card.rank);
  
  return different_colors && descending_rank;
}

// Check if a stack of cards forms a valid tableau sequence
bool FreecellGame::isValidTableauSequence(const std::vector<cardlib::Card>& cards) {
  if (cards.size() <= 1) {
    return true;
  }
  
  for (size_t i = 0; i < cards.size() - 1; i++) {
    const cardlib::Card& upper_card = cards[i];
    const cardlib::Card& lower_card = cards[i + 1];
    
    // Cards must be in alternating colors and descending rank
    bool different_colors = isCardRed(upper_card) != isCardRed(lower_card);
    bool descending_rank = static_cast<int>(upper_card.rank) - 1 == static_cast<int>(lower_card.rank);
    
    if (!different_colors || !descending_rank) {
      return false;
    }
  }
  
  return true;
}

// Helper to determine if a card is red
bool FreecellGame::isCardRed(const cardlib::Card& card) {
  return card.suit == cardlib::Suit::HEARTS || card.suit == cardlib::Suit::DIAMONDS;
}

// Draw function modification to highlight the selected card
void FreecellGame::highlightSelectedCard(cairo_t *cr) {
  if (!cr || selected_pile_ == -1) {
    return;
  }
  
  int x = 0, y = 0;
  
  // Determine position based on pile type
  if (selected_pile_ >= 0 && selected_pile_ <= 3) {
    // Freecell
    x = current_card_spacing_ + selected_pile_ * (current_card_width_ + current_card_spacing_);
    y = current_card_spacing_;
  } else if (selected_pile_ >= 4 && selected_pile_ <= 7) {
    // Foundation
    x = allocation.width - (8 - selected_pile_) * (current_card_width_ + current_card_spacing_);
    y = current_card_spacing_;
  } else if (selected_pile_ >= 8 && selected_pile_ <= 15) {
    // Tableau
    int tableau_idx = selected_pile_ - 8;
    x = current_card_spacing_ + tableau_idx * (current_card_width_ + current_card_spacing_);
    
    // Position depends on the card index in the pile
    if (selected_card_idx_ >= 0 && tableau_idx < tableau_.size() && 
        selected_card_idx_ < tableau_[tableau_idx].size()) {
      y = 2 * current_card_spacing_ + current_card_height_ + 
          selected_card_idx_ * current_vert_spacing_;
    } else {
      // Empty tableau or invalid selection
      y = 2 * current_card_spacing_ + current_card_height_;
    }
  }
  
  // Choose highlight color based on whether we're selecting a card to move
  if (keyboard_selection_active_ && source_pile_ == selected_pile_) {
    // Source card/pile is highlighted in blue
    cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.5); // Semi-transparent blue
  } else {
    // Regular selection is highlighted in yellow
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.5); // Semi-transparent yellow
  }
  
  cairo_set_line_width(cr, 3.0);
  cairo_rectangle(cr, x - 2, y - 2, current_card_width_ + 4, current_card_height_ + 4);
  cairo_stroke(cr);
  
  // If we have a card selected for movement in tableau, highlight all cards below it
  if (keyboard_selection_active_ && source_pile_ >= 8 && source_pile_ <= 15 && source_card_idx_ >= 0) {
    int tableau_idx = source_pile_ - 8;
    
    if (tableau_idx < tableau_.size() && !tableau_[tableau_idx].empty() && 
        source_card_idx_ < tableau_[tableau_idx].size()) {
        
      // Highlight all cards from the selected one to the bottom
      cairo_set_source_rgba(cr, 0.0, 0.5, 1.0, 0.3); // Lighter blue for stack
      
      x = current_card_spacing_ + tableau_idx * (current_card_width_ + current_card_spacing_);
      y = 2 * current_card_spacing_ + current_card_height_ + source_card_idx_ * current_vert_spacing_;
      
      // Draw a single rectangle that covers all cards in the stack
      int stack_height = (tableau_[tableau_idx].size() - source_card_idx_ - 1) * 
                          current_vert_spacing_ + current_card_height_;
      
      if (stack_height > 0) {
        cairo_rectangle(cr, x - 2, y - 2, current_card_width_ + 4, stack_height + 4);
        cairo_stroke(cr);
      }
    }
  }
}

// Reset keyboard navigation
void FreecellGame::resetKeyboardNavigation() {
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;
  source_pile_ = -1;
  source_card_idx_ = -1;
  selected_pile_ = -1;
  selected_card_idx_ = -1;
}

bool FreecellGame::isCardPlayable() {
  // Freecells always have playable cards
  if (selected_pile_ >= 0 && selected_pile_ <= 3) {
    return freecells_[selected_pile_].has_value();
  }
  
  // Foundation cards are only playable if they're at the top
  if (selected_pile_ >= 4 && selected_pile_ <= 7) {
    int foundation_idx = selected_pile_ - 4;
    return !foundation_[foundation_idx].empty() && 
           selected_card_idx_ == static_cast<int>(foundation_[foundation_idx].size() - 1);
  }
  
  // Tableau cards - check if they're in a valid sequence to the bottom
  if (selected_pile_ >= 8 && selected_pile_ <= 15) {
    int tableau_idx = selected_pile_ - 8;
    
    if (tableau_idx < 0 || static_cast<size_t>(tableau_idx) >= tableau_.size() || 
        tableau_[tableau_idx].empty() || selected_card_idx_ < 0 || 
        static_cast<size_t>(selected_card_idx_) >= tableau_[tableau_idx].size()) {
      return false;
    }
    
    // Check if there's a valid sequence from this card to the bottom
    for (size_t i = selected_card_idx_; i < tableau_[tableau_idx].size() - 1; i++) {
      const cardlib::Card& card1 = tableau_[tableau_idx][i];
      const cardlib::Card& card2 = tableau_[tableau_idx][i + 1];
      
      bool different_colors = isCardRed(card1) != isCardRed(card2);
      bool descending_rank = static_cast<int>(card1.rank) == static_cast<int>(card2.rank) + 1;
      
      if (!different_colors || !descending_rank) {
        return false;
      }
    }
    
    return true;
  }
  
  return false;
}

// Function to handle the spacebar action (similar to right-click)
// Fixed function to handle the spacebar action (similar to right-click)
bool FreecellGame::handleSpacebarAction() {
  if (selected_pile_ == -1) {
    return false;
  }

  bool card_moved = false;

  // Source: freecell (0-3)
  if (selected_pile_ < 4 && freecells_[selected_pile_].has_value()) {
    const cardlib::Card card = freecells_[selected_pile_].value();
    
    // Try to find a valid foundation
    int target_foundation = -1;
    for (int i = 0; i < 4; i++) {
      if (canMoveToFoundation(card, i)) {
        target_foundation = i;
        break;
      }
    }
    
    if (target_foundation != -1) {
      // Move card to foundation
      foundation_[target_foundation].push_back(card);
      freecells_[selected_pile_] = std::nullopt;
      
      // Play sound
      playSound(GameSoundEvent::CardPlace);
      
      // Check for win
      if (checkWinCondition()) {
        startWinAnimation();
      }
      
      card_moved = true;
    }
    // If cannot move to foundation, we don't need to check for freecell
    // as the card is already in a freecell
  }
  // Source: foundation (4-7)
  else if (selected_pile_ >= 4 && selected_pile_ < 8) {
    // We typically don't want to auto-move cards from the foundation
    // as that would be counter-productive to winning
    return false;
  }
  // Source: tableau (8-15)
  else if (selected_pile_ >= 8) {
    int tableau_idx = selected_pile_ - 8;
    auto &pile = tableau_[tableau_idx];
    
    if (!pile.empty()) {
      const cardlib::Card &card = pile.back();
      
      // Try to find a valid foundation
      int target_foundation = -1;
      for (int i = 0; i < 4; i++) {
        if (canMoveToFoundation(card, i)) {
          target_foundation = i;
          break;
        }
      }
      
      if (target_foundation != -1) {
        // Move card to foundation
        foundation_[target_foundation].push_back(card);
        pile.pop_back();
        
        // Play sound
        playSound(GameSoundEvent::CardPlace);
        
        // Check for win
        if (checkWinCondition()) {
          startWinAnimation();
        }
        
        card_moved = true;
      } 
      else {
        // If cannot move to foundation, try to move to the first available freecell
        int target_freecell = -1;
        for (int i = 0; i < 4; i++) {
          if (!freecells_[i].has_value()) {
            target_freecell = i;
            break;
          }
        }
        
        if (target_freecell != -1) {
          // Move card to freecell
          freecells_[target_freecell] = card;
          pile.pop_back();
          
          // Play sound
          playSound(GameSoundEvent::CardPlace);
          
          card_moved = true;
        }
      }
    }
  }
  
  // If a card was moved, refresh the display but keep the same pile selected
  if (card_moved) {
    // For tableau, if there's another card now at the top, select it
    if (selected_pile_ >= 8) {
      int tableau_idx = selected_pile_ - 8;
      if (!tableau_[tableau_idx].empty()) {
        selected_card_idx_ = tableau_[tableau_idx].size() - 1;
      } else {
        // If tableau is now empty, just keep the pile selected
        selected_card_idx_ = -1;
      }
    } else if (selected_pile_ < 4) {
      // For freecell, if the cell is now empty, just keep the pile selected
      if (!freecells_[selected_pile_].has_value()) {
        selected_card_idx_ = -1;
      }
    }
    
    refreshDisplay();
    return true;
  }
  
  return false;
}
