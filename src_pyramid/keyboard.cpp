#include "pyramid.h"
#include <gtk/gtk.h>

// Handler for keyboard events
gboolean PyramidGame::onKeyPress(GtkWidget *widget, GdkEventKey *event,
                                   gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

if (game->win_animation_active_) {
  game->stopWinAnimation();
  return TRUE;
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
void PyramidGame::toggleFullscreen() {
  if (is_fullscreen_) {
    gtk_window_unfullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = false;
  } else {
    gtk_window_fullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = true;
  }
}

// Select the next (right) pile
void PyramidGame::selectNextPile() {
  // Calculate max foundation index (depends on game mode)
  int max_foundation_index = 2 + foundation_.size() - 1;
  // Calculate first tableau index
  int first_tableau_index = max_foundation_index + 1;
  // Calculate last tableau index
  int last_tableau_index = first_tableau_index + 6;

  if (selected_pile_ == -1) {
    // Start with stock pile (index 0)
    selected_pile_ = 0;
    selected_card_idx_ = stock_.empty() ? -1 : 0;
  } else {
    // Move to the next pile
    selected_pile_++;

    // Wrap around if we're past the last tableau pile
    if (selected_pile_ > last_tableau_index) {
      selected_pile_ = 0;
    }

    // Set card index based on the selected pile
    if (selected_pile_ == 0) {
      selected_card_idx_ = stock_.empty() ? -1 : 0;
    } else if (selected_pile_ == 1) {
      selected_card_idx_ = waste_.empty() ? -1 : waste_.size() - 1;
    } else if (selected_pile_ >= 2 && selected_pile_ <= max_foundation_index) {
      int foundation_idx = selected_pile_ - 2;
      if (foundation_idx >= 0 && foundation_idx < foundation_.size()) {
        selected_card_idx_ = foundation_[foundation_idx].empty()
                              ? -1
                              : foundation_[foundation_idx].size() - 1;
      } else {
        selected_card_idx_ = -1;
      }
    } else if (selected_pile_ >= first_tableau_index && selected_pile_ <= last_tableau_index) {
      int tableau_idx = selected_pile_ - first_tableau_index;
      if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
        // Find the bottom-most face-up card
        int highest_face_up = -1;
        for (int i = 0; i < tableau_[tableau_idx].size(); i++) {
          if (tableau_[tableau_idx][i].face_up) {
            highest_face_up = i;
          }
        }
        
        selected_card_idx_ = tableau_[tableau_idx].empty() 
                               ? -1 
                               : (highest_face_up >= 0 
                                 ? tableau_[tableau_idx].size() - 1 
                                 : highest_face_up);
      } else {
        selected_card_idx_ = -1;
      }
    }
  }

  refreshDisplay();
}

// Select the previous (left) pile
void PyramidGame::selectPreviousPile() {
  // Calculate max foundation index (depends on game mode)
  int max_foundation_index = 2 + foundation_.size() - 1;
  // Calculate first tableau index
  int first_tableau_index = max_foundation_index + 1;
  // Calculate last tableau index
  int last_tableau_index = first_tableau_index + 6;

  if (selected_pile_ == -1) {
    // Start with the last tableau pile
    selected_pile_ = last_tableau_index;
    if (tableau_.size() > 6) {
      selected_card_idx_ = tableau_[6].empty() ? -1 : tableau_[6].size() - 1;
    } else {
      selected_card_idx_ = -1;
    }
  } else {
    // Move to the previous pile
    selected_pile_--;

    // Wrap around if we're before the stock pile (index 0)
    if (selected_pile_ < 0) {
      selected_pile_ = last_tableau_index;
    }

    // Set card index based on the selected pile
    if (selected_pile_ == 0) {
      selected_card_idx_ = stock_.empty() ? -1 : 0;
    } else if (selected_pile_ == 1) {
      selected_card_idx_ = waste_.empty() ? -1 : waste_.size() - 1;
    } else if (selected_pile_ >= 2 && selected_pile_ <= max_foundation_index) {
      int foundation_idx = selected_pile_ - 2;
      if (foundation_idx >= 0 && foundation_idx < foundation_.size()) {
        selected_card_idx_ = foundation_[foundation_idx].empty()
                                ? -1
                                : foundation_[foundation_idx].size() - 1;
      } else {
        selected_card_idx_ = -1;
      }
    } else if (selected_pile_ >= first_tableau_index && selected_pile_ <= last_tableau_index) {
      int tableau_idx = selected_pile_ - first_tableau_index;
      if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
        // Find the bottom-most face-up card
        int highest_face_up = -1;
        for (int i = 0; i < tableau_[tableau_idx].size(); i++) {
          if (tableau_[tableau_idx][i].face_up) {
            highest_face_up = i;
          }
        }
        
        selected_card_idx_ = tableau_[tableau_idx].empty() 
                               ? -1 
                               : (highest_face_up == -1 
                                 ? tableau_[tableau_idx].size() - 1 
                                 : tableau_[tableau_idx].size() - 1);
      } else {
        selected_card_idx_ = -1;
      }
    }
  }

  refreshDisplay();
}

// Move selection up in a tableau pile
void PyramidGame::selectCardUp() {
  // Calculate max foundation index (depends on game mode)
  int max_foundation_index = 2 + foundation_.size() - 1;
  // Calculate first tableau index
  int first_tableau_index = max_foundation_index + 1;
  // Calculate last tableau index
  int last_tableau_index = first_tableau_index + 6;

  if (selected_pile_ < first_tableau_index || selected_pile_ > last_tableau_index) {
    return;
  }

  int tableau_idx = selected_pile_ - first_tableau_index;
  if (tableau_idx < 0 || tableau_idx >= tableau_.size()) {
    return;
  }

  // If the tableau pile is empty, move to the corresponding pile in the top row
  if (tableau_[tableau_idx].empty()) {
    if (tableau_idx == 0) {
      // Pile 0 goes to stock pile
      selected_pile_ = 0;
      selected_card_idx_ = stock_.empty() ? -1 : 0;
    } else if (tableau_idx == 1) {
      // Pile 1 goes to waste pile
      selected_pile_ = 1;
      selected_card_idx_ = waste_.empty() ? -1 : waste_.size() - 1;
    } else if (tableau_idx >= 2) {
      // Other piles go to corresponding foundation (if available)
      int foundation_idx = tableau_idx - 2;
      if (foundation_idx >= 0 && foundation_idx < foundation_.size()) {
        selected_pile_ = 2 + foundation_idx;
        selected_card_idx_ = foundation_[foundation_idx].empty()
                                ? -1
                                : foundation_[foundation_idx].size() - 1;
      }
    }
    refreshDisplay();
    return;
  }

  // Ensure selected_card_idx_ is valid
  if (selected_card_idx_ < 0 ||
      selected_card_idx_ >= static_cast<int>(tableau_[tableau_idx].size())) {
    // Reset to a valid state - select the top card
    selected_card_idx_ = tableau_[tableau_idx].size() - 1;
    refreshDisplay();
    return;
  }

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
    if (foundation_idx >= 0 && foundation_idx < foundation_.size()) {
      selected_pile_ = 2 + foundation_idx;
      selected_card_idx_ = foundation_[foundation_idx].empty()
                             ? -1
                             : foundation_[foundation_idx].size() - 1;
    }
  }

  refreshDisplay();
}

// Move selection down in a tableau pile
// Move selection down in a tableau pile
void PyramidGame::selectCardDown() {
  // Calculate max foundation index (depends on game mode)
  int max_foundation_index = 2 + foundation_.size() - 1;
  // Calculate first tableau index
  int first_tableau_index = max_foundation_index + 1;
  // Calculate last tableau index
  int last_tableau_index = first_tableau_index + 6;

  // If we're in the top row, move down to the corresponding tableau pile
  if (selected_pile_ >= 0 && selected_pile_ <= max_foundation_index) {
    int target_tableau;

    // Direct mapping from top row to tableau piles
    if (selected_pile_ == 0) {
      // Stock pile goes to tableau pile 0 (first tableau)
      target_tableau = 0;
    } else if (selected_pile_ == 1) {
      // Waste pile goes to tableau pile 1 (second tableau)
      target_tableau = 1;
    } else {
      // Foundation piles map to tableau piles 2-6
      // Foundation 0 (pile 2) -> Tableau 2
      // Foundation 1 (pile 3) -> Tableau 3
      // Foundation 2 (pile 4) -> Tableau 4
      // Foundation 3 (pile 5) -> Tableau 5
      // Foundation 4+ (pile 6+) -> Tableau 6 (last tableau)
      
      int foundation_idx = selected_pile_ - 2;
      
      if (foundation_idx <= 4) {
        // Direct mapping for first 5 foundations (0-4)
        target_tableau = foundation_idx + 2;
      } else {
        // Any foundation beyond the 5th goes to the 7th tableau (index 6)
        target_tableau = 6;
      }
    }

    // Check if the target_tableau is within range and valid
    if (target_tableau >= 0 && target_tableau < tableau_.size()) {
      selected_pile_ = first_tableau_index + target_tableau;
      
      // Select the bottom-most card in the tableau
      selected_card_idx_ = tableau_[target_tableau].empty() ? 
                           -1 : 
                           tableau_[target_tableau].size() - 1;
    }
  }
  // If we're already in the tableau, try to move down
  else if (selected_pile_ >= first_tableau_index && selected_pile_ <= last_tableau_index) {
    int tableau_idx = selected_pile_ - first_tableau_index;
    if (tableau_idx >= 0 && tableau_idx < tableau_.size() && !tableau_[tableau_idx].empty()) {
      // If no card selected yet, select the top card
      if (selected_card_idx_ < 0) {
        selected_card_idx_ = 0;
      }
      // Try to move down to next card
      else if (selected_card_idx_ < tableau_[tableau_idx].size() - 1) {
        selected_card_idx_++;
      }
    }
  }

  refreshDisplay();
}

void PyramidGame::activateSelected() {
  if (selected_pile_ == -1) {
    return;
  }

  // Calculate max foundation index (depends on game mode)
  int max_foundation_index = 2 + foundation_.size() - 1;
  // Calculate first tableau index
  int first_tableau_index = max_foundation_index + 1;
  // Calculate last tableau index
  int last_tableau_index = first_tableau_index + 6;

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

  // Handle stock pile (draw cards)
  if (selected_pile_ == 0) {
    handleStockPileClick();
    return;
  }

  // Try to move to foundation first
  if (selected_pile_ == 1 || (selected_pile_ >= first_tableau_index && selected_pile_ <= last_tableau_index)) {
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
        startFoundationMoveAnimation(*card, selected_pile_, 0,
                                     target_foundation + 2);

        // Remove card from waste pile
        waste_.pop_back();

        // Update selection to point to new top card
        selected_card_idx_ = waste_.empty() ? -1 : waste_.size() - 1;
        return;
      }
    } else if (selected_pile_ >= first_tableau_index && selected_pile_ <= last_tableau_index) {
      int tableau_idx = selected_pile_ - first_tableau_index;
      if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
        auto &tableau_pile = tableau_[tableau_idx];

        if (!tableau_pile.empty() && selected_card_idx_ >= 0 &&
            selected_card_idx_ < tableau_pile.size() &&
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
            if (static_cast<size_t>(selected_card_idx_) ==
                tableau_pile.size() - 1) {
              // Start animation
              startFoundationMoveAnimation(*card, selected_pile_,
                                          selected_card_idx_,
                                          target_foundation + 2);

              // Remove card from tableau
              tableau_pile.pop_back();

              // Flip new top card if needed
              if (!tableau_pile.empty() && !tableau_pile.back().face_up) {
                tableau_pile.back().face_up = true;
                playSound(GameSoundEvent::CardFlip);
              }

              // Update selection to point to new top card
              selected_card_idx_ =
                  tableau_pile.empty() ? -1 : tableau_pile.size() - 1;
              return;
            }
          }
        }
      }
    }
  }

  // If we get here, we couldn't move to foundation
  // For tableau piles, activate selection for moving between piles
  if (selected_pile_ >= first_tableau_index && selected_pile_ <= last_tableau_index) {
    int tableau_idx = selected_pile_ - first_tableau_index;
    if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
      auto &tableau_pile = tableau_[tableau_idx];

      if (!tableau_pile.empty() && selected_card_idx_ >= 0 &&
          selected_card_idx_ < tableau_pile.size() &&
          tableau_pile[selected_card_idx_].face_up) {
        // Activate selection for this card
        keyboard_selection_active_ = true;
        source_pile_ = selected_pile_;
        source_card_idx_ = selected_card_idx_;
        // Force a refresh immediately to show the blue highlight
        refreshDisplay();
      }
    }
  } else if (selected_pile_ == 1 && !waste_.empty()) {
    // Allow selecting waste pile top card
    keyboard_selection_active_ = true;
    source_pile_ = selected_pile_;
    source_card_idx_ = waste_.size() - 1;
    // Force a refresh immediately to show the blue highlight
    refreshDisplay();
  } else if (selected_pile_ >= 2 && selected_pile_ <= max_foundation_index) {
    int foundation_idx = selected_pile_ - 2;
    if (foundation_idx >= 0 && foundation_idx < foundation_.size()) {
      auto &foundation_pile = foundation_[foundation_idx];

      if (!foundation_pile.empty()) {
        // Allow selecting foundation pile top card
        keyboard_selection_active_ = true;
        source_pile_ = selected_pile_;
        source_card_idx_ = foundation_pile.size() - 1;
        // Force a refresh immediately to show the blue highlight
        refreshDisplay();
      }
    }
  }
}

bool PyramidGame::tryMoveSelectedCard() {
  if (source_pile_ == -1 || selected_pile_ == -1) {
    return false;
  }

  // Calculate max foundation index (depends on game mode)
  int max_foundation_index = 2 + foundation_.size() - 1;
  // Calculate first tableau index
  int first_tableau_index = max_foundation_index + 1;
  // Calculate last tableau index
  int last_tableau_index = first_tableau_index + 6;

  // Validate source_pile_ and source_card_idx_
  if (source_pile_ == 1) {
    if (waste_.empty() || source_card_idx_ < 0 ||
        source_card_idx_ >= static_cast<int>(waste_.size())) {
      return false;
    }
  } else if (source_pile_ >= 2 && source_pile_ <= max_foundation_index) {
    // Foundation pile validation
    int foundation_idx = source_pile_ - 2;
    if (foundation_idx < 0 ||
        foundation_idx >= static_cast<int>(foundation_.size()) ||
        foundation_[foundation_idx].empty() || source_card_idx_ < 0 ||
        source_card_idx_ >=
            static_cast<int>(foundation_[foundation_idx].size())) {
      return false;
    }
  } else if (source_pile_ >= first_tableau_index && source_pile_ <= last_tableau_index) {
    int tableau_idx = source_pile_ - first_tableau_index;
    if (tableau_idx < 0 || tableau_idx >= static_cast<int>(tableau_.size()) ||
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
  } else if (source_pile_ >= 2 && source_pile_ <= max_foundation_index) {
    // Foundation pile - can only move top card
    int foundation_idx = source_pile_ - 2;
    if (foundation_idx >= 0 && foundation_idx < foundation_.size() && !foundation_[foundation_idx].empty()) {
      source_cards = {foundation_[foundation_idx].back()};
    }
  } else if (source_pile_ >= first_tableau_index && source_pile_ <= last_tableau_index) {
    // Tableau pile - can move the selected card and all cards below it
    int tableau_idx = source_pile_ - first_tableau_index;
    if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
      auto &source_tableau = tableau_[tableau_idx];

      if (!source_tableau.empty() && source_card_idx_ >= 0 &&
          static_cast<size_t>(source_card_idx_) < source_tableau.size() &&
          source_tableau[source_card_idx_].face_up) {

        for (size_t i = source_card_idx_; i < source_tableau.size(); i++) {
          source_cards.push_back(source_tableau[i].card);
        }
      }
    }
  }

  // Get target cards
  if (selected_pile_ >= 2 && selected_pile_ <= max_foundation_index) {
    // Foundation pile
    int foundation_idx = selected_pile_ - 2;
    if (foundation_idx >= 0 && foundation_idx < foundation_.size()) {
      target_cards = foundation_[foundation_idx];
    }
  } else if (selected_pile_ >= first_tableau_index && selected_pile_ <= last_tableau_index) {
    // Tableau pile
    int tableau_idx = selected_pile_ - first_tableau_index;
    if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
      auto &target_tableau = tableau_[tableau_idx];

      if (!target_tableau.empty()) {
        target_cards = {target_tableau.back().card};
      }
      // Empty pile case - leave target_cards empty
    }
  }

  // Check if the move is valid
  bool is_foundation = (selected_pile_ >= 2 && selected_pile_ <= max_foundation_index);
  if (source_cards.empty() ||
      !canMoveToPile(source_cards, target_cards, is_foundation)) {
    return false;
  }

  // Execute the move
  if (source_pile_ == 1) {
    // Moving from waste pile
    // Remove card from waste
    waste_.pop_back();

    if (is_foundation) {
      // Add to foundation
      int foundation_idx = selected_pile_ - 2;
      if (foundation_idx >= 0 && foundation_idx < foundation_.size()) {
        foundation_[foundation_idx].push_back(source_cards[0]);
      }
    } else {
      // Add to tableau
      int tableau_idx = selected_pile_ - first_tableau_index;
      if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
        tableau_[tableau_idx].emplace_back(source_cards[0], true);
      }
    }
  } else if (source_pile_ >= 2 && source_pile_ <= max_foundation_index) {
    // Moving from foundation pile
    int foundation_idx = source_pile_ - 2;

    // Can only move to tableau (not to another foundation)
    if (!is_foundation && selected_pile_ >= first_tableau_index && selected_pile_ <= last_tableau_index) {
      // Remove card from foundation
      if (foundation_idx >= 0 && foundation_idx < foundation_.size() && !foundation_[foundation_idx].empty()) {
        cardlib::Card card_to_move = foundation_[foundation_idx].back();
        foundation_[foundation_idx].pop_back();

        // Add to tableau
        int tableau_idx = selected_pile_ - first_tableau_index;
        if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
          tableau_[tableau_idx].emplace_back(card_to_move, true);
        }
      }
    } else {
      // Invalid move (can't move between foundations)
      return false;
    }
  } else if (source_pile_ >= first_tableau_index && source_pile_ <= last_tableau_index) {
    // Moving from tableau pile
    int source_tableau_idx = source_pile_ - first_tableau_index;
    if (source_tableau_idx >= 0 && source_tableau_idx < tableau_.size()) {
      auto &source_tableau = tableau_[source_tableau_idx];

      // Store cards to move
      std::vector<TableauCard> cards_to_move;
      for (size_t i = source_card_idx_; i < source_tableau.size(); i++) {
        cards_to_move.push_back(source_tableau[i]);
      }

      // Remove cards from source tableau
      source_tableau.erase(source_tableau.begin() + source_card_idx_,
                           source_tableau.end());

      // Flip the new top card if needed
      if (!source_tableau.empty() && !source_tableau.back().face_up) {
        source_tableau.back().face_up = true;
        playSound(GameSoundEvent::CardFlip);
      }

      if (is_foundation) {
        // Add to foundation (should be only one card)
        int foundation_idx = selected_pile_ - 2;
        if (foundation_idx >= 0 && foundation_idx < foundation_.size()) {
          foundation_[foundation_idx].push_back(source_cards[0]);
        }
      } else {
        // Add to target tableau
        int target_tableau_idx = selected_pile_ - first_tableau_index;
        if (target_tableau_idx >= 0 && target_tableau_idx < tableau_.size()) {
          tableau_[target_tableau_idx].insert(tableau_[target_tableau_idx].end(),
                                            cards_to_move.begin(),
                                            cards_to_move.end());
        }
      }
    }
  }

  // Update selected card index after move
  if (selected_pile_ >= first_tableau_index && selected_pile_ <= last_tableau_index) {
    int tableau_idx = selected_pile_ - first_tableau_index;
    if (tableau_idx >= 0 && tableau_idx < tableau_.size()) {
      selected_card_idx_ =
          tableau_[tableau_idx].empty() ? -1 : tableau_[tableau_idx].size() - 1;
    }
  }

  refreshDisplay();
  return true;
}

void PyramidGame::resetKeyboardNavigation() {
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;
  source_pile_ = -1;
  source_card_idx_ = -1;
  selected_pile_ = -1;
  selected_card_idx_ = -1;
}
