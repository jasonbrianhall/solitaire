#include "spider.h"
#include <gtk/gtk.h>

// Handler for keyboard events
gboolean SolitaireGame::onKeyPress(GtkWidget *widget, GdkEventKey *event,
                                   gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

if (game->win_animation_active_) {
  game->stopWinAnimation();
  return TRUE;
}

if (game->sequence_animation_active_)  {
  return TRUE;
}

  // Block keyboard input if auto-finish is active, except for Escape to cancel
  if (game->auto_finish_active_) {
    if (event->keyval == GDK_KEY_Escape) {
      // Allow Escape to cancel auto-finish
      game->auto_finish_active_ = false;
      if (game->auto_finish_timer_id_ > 0) {
        g_source_remove(game->auto_finish_timer_id_);
        game->auto_finish_timer_id_ = 0;
      }
      game->resetKeyboardNavigation();
      game->refreshDisplay();
      return TRUE;
    }
    return FALSE; // Block all other keyboard input during auto-finish
  }

  // Check for control key modifier
  bool ctrl_pressed = (event->state & GDK_CONTROL_MASK);

  // If any animation is active, block all keyboard input except Escape
  if (game->foundation_move_animation_active_ ||
      game->stock_to_waste_animation_active_ || game->deal_animation_active_ ||
      game->win_animation_active_) {
    if (event->keyval == GDK_KEY_Escape) {
      // Allow Escape to cancel animations
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

  case GDK_KEY_l:
  case GDK_KEY_L:
    if (ctrl_pressed) {
      // Show deck loading dialog
      GtkWidget *dialog = gtk_file_chooser_dialog_new(
          "Load Deck", GTK_WINDOW(game->window_),
          GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
          "_Open", GTK_RESPONSE_ACCEPT, NULL);

      GtkFileFilter *filter = gtk_file_filter_new();
      gtk_file_filter_set_name(filter, "Card Deck Files (*.zip)");
      gtk_file_filter_add_pattern(filter, "*.zip");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);  

      if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (game->loadDeck(filename)) {
          game->refreshDisplay();
        }
        g_free(filename);
      }

      gtk_widget_destroy(dialog);
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
      GList *menu_items =
          gtk_container_get_children(GTK_CONTAINER(game->vbox_));
      if (menu_items) {
        GtkWidget *menubar = GTK_WIDGET(menu_items->data);
        GList *menus = gtk_container_get_children(GTK_CONTAINER(menubar));
        if (menus) {
          // First menu should be the Game menu
          GtkWidget *game_menu_item = GTK_WIDGET(menus->data);
          GtkWidget *game_menu =
              gtk_menu_item_get_submenu(GTK_MENU_ITEM(game_menu_item));
          if (game_menu) {
            GList *game_menu_items =
                gtk_container_get_children(GTK_CONTAINER(game_menu));
            // Find the sound checkbox (should be near the end)
            for (GList *item = game_menu_items; item != NULL;
                 item = item->next) {
              if (GTK_IS_CHECK_MENU_ITEM(item->data)) {
                GtkWidget *check_item = GTK_WIDGET(item->data);
                const gchar *label =
                    gtk_menu_item_get_label(GTK_MENU_ITEM(check_item));
                if (label && strstr(label, "Sound") != NULL) {
                  gtk_check_menu_item_set_active(
                      GTK_CHECK_MENU_ITEM(check_item), game->sound_enabled_);
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
    // Move to the next pile, with special handling for Spider
    if (selected_pile_ == 0) {
      // From stock, jump to first tableau pile
      selected_pile_ = 6;
    } else if (selected_pile_ >= 6 && selected_pile_ < 15) {
      // Navigate through tableau piles
      selected_pile_++;
    } else if (selected_pile_ >= 15) {
      // Wrap around to stock
      selected_pile_ = 0;
    }

    // Set card index based on the selected pile
    if (selected_pile_ == 0) {
      selected_card_idx_ = stock_.empty() ? -1 : 0;
    } else if (selected_pile_ >= 6 && selected_pile_ <= 15) {
      int tableau_idx = selected_pile_ - 6;
      selected_card_idx_ = 
          tableau_[tableau_idx].empty() ? -1 : tableau_[tableau_idx].size() - 1;
    }
  }

  refreshDisplay();
}


// Select the previous (left) pile
void SolitaireGame::selectPreviousPile() {
  if (selected_pile_ == -1) {
    // Start with the last tableau pile
    selected_pile_ = 15;
    selected_card_idx_ = 
        tableau_[9].empty() ? -1 : tableau_[9].size() - 1;
  } else {
    // Move to the previous pile with special Spider handling
    if (selected_pile_ == 0) {
      // From stock, wrap to last tableau pile
      selected_pile_ = 15;
    } else if (selected_pile_ > 6 && selected_pile_ <= 15) {
      // Navigate backward through tableau
      selected_pile_--;
    } else if (selected_pile_ == 6) {
      // From first tableau to stock
      selected_pile_ = 0;
    }

    // Set card index based on the selected pile
    if (selected_pile_ == 0) {
      selected_card_idx_ = stock_.empty() ? -1 : 0;
    } else if (selected_pile_ >= 6 && selected_pile_ <= 15) {
      int tableau_idx = selected_pile_ - 6;
      selected_card_idx_ = 
          tableau_[tableau_idx].empty() ? -1 : tableau_[tableau_idx].size() - 1;
    }
  }

  refreshDisplay();
}

// Move selection up in a tableau pile
void SolitaireGame::selectCardUp() {
  // Only handle tableau piles (6-15)
  if (selected_pile_ < 6 || selected_pile_ > 15) {
    return;
  }

  int tableau_idx = selected_pile_ - 6;
  if (tableau_idx < 0 || tableau_idx >= tableau_.size()) {
    return;
  }

  // If the tableau pile is empty, move to stock
  if (tableau_[tableau_idx].empty()) {
    selected_pile_ = 0;
    selected_card_idx_ = stock_.empty() ? -1 : 0;
    refreshDisplay();
    return;
  }

  // Ensure selected_card_idx_ is valid
  if (selected_card_idx_ < 0 ||
      selected_card_idx_ >= static_cast<int>(tableau_[tableau_idx].size())) {
    selected_card_idx_ = tableau_[tableau_idx].size() - 1;
    return;
  }

  // Try to navigate up within the tableau pile
  if (selected_card_idx_ > 0) {
    for (int i = selected_card_idx_ - 1; i >= 0; i--) {
      // Only select face up cards that are ALSO valid drag sources
      if (tableau_[tableau_idx][i].face_up && isValidDragSource(selected_pile_, i)) {
        selected_card_idx_ = i;
        refreshDisplay();
        return;
      }
    }
  }

  // If we couldn't navigate up or we're at the top card, move to stock
  selected_pile_ = 0;
  selected_card_idx_ = stock_.empty() ? -1 : 0;
  refreshDisplay();
}

// Move selection down in a tableau pile
void SolitaireGame::selectCardDown() {
  // If we're at the stock, move to the first tableau pile
  if (selected_pile_ == 0) {
    selected_pile_ = 6;
    selected_card_idx_ = tableau_[0].empty() ? -1 : tableau_[0].size() - 1;
    refreshDisplay();
    return;
  }
  
  // If we're already in the tableau, try to move down
  if (selected_pile_ >= 6 && selected_pile_ <= 15) {
    int tableau_idx = selected_pile_ - 6;
    if (!tableau_[tableau_idx].empty() && selected_card_idx_ >= 0) {
      if (static_cast<size_t>(selected_card_idx_) <
          tableau_[tableau_idx].size() - 1) {
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
  if (foundation_move_animation_active_ || stock_to_waste_animation_active_ ||
      deal_animation_active_ || win_animation_active_) {
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
    playSound(GameSoundEvent::CardPlace);
    refreshDisplay();
    return;
  }

  // Handle stock pile (deal cards)
  if (selected_pile_ == 0) {
    handleStockPileClick();
    return;
  }

  // For tableau piles, activate selection for moving between piles
  if (selected_pile_ >= 6 && selected_pile_ <= 15) {
    int tableau_idx = selected_pile_ - 6;
    auto &tableau_pile = tableau_[tableau_idx];

    if (!tableau_pile.empty() && selected_card_idx_ >= 0 &&
        tableau_pile[selected_card_idx_].face_up) {
          // Use existing isValidDragSource function to check if this is a valid selection
      if (isValidDragSource(selected_pile_, selected_card_idx_)) {
        // Activate selection for this card
        keyboard_selection_active_ = true;
        source_pile_ = selected_pile_;
        source_card_idx_ = selected_card_idx_;
        // Force a refresh immediately to show the blue highlight
        refreshDisplay();
      } else {
        // Provide feedback that this selection isn't valid
        playSound(GameSoundEvent::CardPlace); // Or another appropriate sound
      }
    }
  }
}

bool SolitaireGame::tryMoveSelectedCard() {
  if (source_pile_ == -1 || selected_pile_ == -1) {
    return false;
  }

  // In Spider, only tableau to tableau moves are valid
  if (source_pile_ < 6 || source_pile_ > 15) {
    return false;
  }

  // Don't allow moving to the same pile
  if (source_pile_ == selected_pile_) {
    return false;
  }

  // Only tableau piles can be targets
  if (selected_pile_ < 6 || selected_pile_ > 15) {
    return false;
  }

  int source_tableau_idx = source_pile_ - 6;
  int target_tableau_idx = selected_pile_ - 6;
  
  // Validate indices
  if (source_tableau_idx < 0 || source_tableau_idx >= static_cast<int>(tableau_.size()) ||
      target_tableau_idx < 0 || target_tableau_idx >= static_cast<int>(tableau_.size())) {
    return false;
  }

  auto &source_tableau = tableau_[source_tableau_idx];
  auto &target_tableau = tableau_[target_tableau_idx];

  // Validate source_card_idx_
  if (source_tableau.empty() || source_card_idx_ < 0 ||
      source_card_idx_ >= static_cast<int>(source_tableau.size()) ||
      !source_tableau[source_card_idx_].face_up) {
    return false;
  }

  // Get the cards to move
  std::vector<TableauCard> cards_to_move;
  for (size_t i = source_card_idx_; i < source_tableau.size(); i++) {
    cards_to_move.push_back(source_tableau[i]);
  }

  // Prepare card vectors for validation using the existing canMoveToPile function
  std::vector<cardlib::Card> source_cards;
  std::vector<cardlib::Card> target_cards;
  
  // Get all source cards (selected card and all below it)
  for (size_t i = source_card_idx_; i < source_tableau.size(); i++) {
    source_cards.push_back(source_tableau[i].card);
  }
  
  // Get target card (top card of target pile)
  if (!target_tableau.empty()) {
    target_cards = {target_tableau.back().card};
  }
  
  // Use the existing canMoveToPile function that handles all Spider rules
  bool is_valid_move = canMoveToPile(source_cards, target_cards, false);

  if (!is_valid_move) {
    return false;
  }

  // Execute the move
  // Store cards to move
  std::vector<TableauCard> cards_to_move_copy = cards_to_move;

  // Remove cards from source tableau
  source_tableau.erase(source_tableau.begin() + source_card_idx_,
                      source_tableau.end());

  // Flip the new top card if needed
  if (!source_tableau.empty() && !source_tableau.back().face_up) {
    source_tableau.back().face_up = true;
    playSound(GameSoundEvent::CardFlip);
  }

  // Add to target tableau
  target_tableau.insert(target_tableau.end(),
                       cards_to_move_copy.begin(),
                       cards_to_move_copy.end());

  // Check if this move completed a sequence
  checkForCompletedSequence(target_tableau_idx);

  // Update selected card index after move
  selected_card_idx_ =
      target_tableau.empty() ? -1 : target_tableau.size() - 1;

  refreshDisplay();
  return true;
}

// 7. Fix card highlighting for Spider


void SolitaireGame::resetKeyboardNavigation() {
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;
  source_pile_ = -1;
  source_card_idx_ = -1;
  selected_pile_ = -1;
  selected_card_idx_ = -1;
}
