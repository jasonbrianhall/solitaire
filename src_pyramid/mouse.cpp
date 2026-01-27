#include "pyramid.h"
#include <iostream>

// Pyramid Solitaire - Mouse input handling
// Cards are removed when paired (two cards sum to 13) or King alone

gboolean PyramidGame::onButtonPress(GtkWidget *widget, GdkEventButton *event,
                                      gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  game->keyboard_navigation_active_ = false;
  game->keyboard_selection_active_ = false;

  if (game->win_animation_active_) {
    game->stopWinAnimation();
    return TRUE;
  }

  if (game->foundation_move_animation_active_ ||
      game->stock_to_waste_animation_active_) {
    return TRUE;
  }

  if (event->button == 1) { // Left click
    auto [pile_index, card_index] = game->getPileAt(event->x, event->y);

    if (pile_index == 0) { // Stock pile
      game->handleStockPileClick();
      return TRUE;
    }

    // In Pyramid, we can only pick up exposed cards from pyramid or waste
    if (pile_index >= 0 && game->isValidDragSource(pile_index, card_index)) {
      game->dragging_ = true;
      game->drag_source_pile_ = pile_index;
      game->drag_start_x_ = event->x;
      game->drag_start_y_ = event->y;
      game->drag_cards_ = game->getDragCards(pile_index, card_index);
      game->playSound(GameSoundEvent::CardFlip);

      // Calculate drag offsets
      int max_foundation_index = 2 + game->foundation_.size() - 1;
      int first_tableau_index = max_foundation_index + 1;

      if (pile_index >= 2 && pile_index <= max_foundation_index) {
        // Foundation piles - horizontal arrangement
        int x_offset_multiplier = 3 + (pile_index - 2);
        game->drag_offset_x_ =
            event->x - (game->current_card_spacing_ +
                        x_offset_multiplier * (game->current_card_width_ +
                                               game->current_card_spacing_));
        game->drag_offset_y_ = event->y - game->current_card_spacing_;
      } else if (pile_index == 1) {
        // Waste pile - single card
        game->drag_offset_x_ =
            event->x - (2 * game->current_card_spacing_ + game->current_card_width_);
        game->drag_offset_y_ = event->y - game->current_card_spacing_;
      } else if (pile_index == 0) {
        // Stock pile - single card
        game->drag_offset_x_ =
            event->x - game->current_card_spacing_;
        game->drag_offset_y_ = event->y - game->current_card_spacing_;
      } else if (pile_index >= first_tableau_index) {
        // PYRAMID CARDS - horizontal within rows, vertical between rows
        // Use same position calculation logic as getPileAt()
        int tableau_idx = pile_index - first_tableau_index;
        if (tableau_idx >= 0 && static_cast<size_t>(tableau_idx) < game->tableau_.size()) {
          const int base_y = game->current_card_spacing_ + game->current_card_height_ + game->current_vert_spacing_;
          const int HORIZ_SPACING = game->current_card_width_ + 15;    // Must match getPileAt
          const int VERT_OVERLAP = game->current_card_height_ / 2;     // Must match getPileAt
          
          // Get actual window width instead of hardcoded value
          GtkAllocation allocation;
          gtk_widget_get_allocation(game->game_area_, &allocation);
          int screen_width = allocation.width;
          
          int row = tableau_idx;
          int num_cards_in_row = row + 1;
          int row_width = game->current_card_width_ + (num_cards_in_row - 1) * HORIZ_SPACING;
          int row_start_x = (screen_width - row_width) / 2;
          int row_y = base_y + row * VERT_OVERLAP;
          
          int card_x = row_start_x + (card_index * HORIZ_SPACING);
          
          game->drag_offset_x_ = event->x - card_x;
          game->drag_offset_y_ = event->y - row_y;
        }
      }
    }
  } else if (event->button == 3) { // Right click
    game->autoFinishGame();
    return TRUE;
  }

  return TRUE;
}

gboolean PyramidGame::onButtonRelease(GtkWidget *widget,
                                        GdkEventButton *event, gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);
  game->keyboard_navigation_active_ = false;

  if (event->button == 1 && game->dragging_) {
    auto [target_pile, card_index] = game->getPileAt(event->x, event->y);

    if (target_pile >= 0) {
      bool move_successful = false;

      int max_foundation_index = 2 + game->foundation_.size() - 1;
      int first_tableau_index = max_foundation_index + 1;

      // In Pyramid Solitaire:
      // - Can't drag to foundation piles
      // - Can only drag to pyramid/waste cards to pair them
      // - Pair removal: two cards that sum to 13, or King alone

      if (target_pile >= first_tableau_index) {
        // Dragging to a pyramid card
        int tableau_idx = target_pile - first_tableau_index;
        if (tableau_idx >= 0 && static_cast<size_t>(tableau_idx) < game->tableau_.size()) {
          auto &tableau_pile = game->tableau_[tableau_idx];

          // Get the target card
          if (!tableau_pile.empty()) {
            std::vector<cardlib::Card> target_cards = {tableau_pile.back().card};

            // Check if cards can be paired (sum to 13)
            if (game->canMoveToPile(game->drag_cards_, target_cards, false)) {
              // Move both cards to discard pile
              
              // Remove from source
              int source_tableau_idx = -1;
              if (game->drag_source_pile_ >= first_tableau_index) {
                source_tableau_idx = game->drag_source_pile_ - first_tableau_index;
                if (source_tableau_idx >= 0 && static_cast<size_t>(source_tableau_idx) < game->tableau_.size()) {
                  auto &source_tableau = game->tableau_[source_tableau_idx];
                  if (!source_tableau.empty()) {
                    game->foundation_[0].push_back(source_tableau.back().card);  // Move to discard
                    source_tableau.pop_back();
                    // Flip new top card if exists
                    if (!source_tableau.empty() && !source_tableau.back().face_up) {
                      source_tableau.back().face_up = true;
                    }
                  }
                }
              } else if (game->drag_source_pile_ == 1) {
                // From waste pile
                if (!game->waste_.empty()) {
                  game->foundation_[0].push_back(game->waste_.back());  // Move to discard
                  game->waste_.pop_back();
                }
              }

              // Move target to discard
              if (!tableau_pile.empty()) {
                game->foundation_[0].push_back(tableau_pile.back().card);  // Move to discard
                tableau_pile.pop_back();
                // Flip new top card if exists
                if (!tableau_pile.empty() && !tableau_pile.back().face_up) {
                  tableau_pile.back().face_up = true;
                }
              }

              move_successful = true;
              game->playSound(GameSoundEvent::CardPlace);
            }
          }
        }
      } else if (target_pile == 1) {
        // Dragging to waste pile - check if waste card and dragged card pair
        if (!game->waste_.empty()) {
          std::vector<cardlib::Card> waste_card = {game->waste_.back()};

          if (game->canMoveToPile(game->drag_cards_, waste_card, false)) {
            // Move both to discard pile
            
            // Remove dragged card
            int source_tableau_idx = -1;
            if (game->drag_source_pile_ >= first_tableau_index) {
              source_tableau_idx = game->drag_source_pile_ - first_tableau_index;
              if (source_tableau_idx >= 0 && static_cast<size_t>(source_tableau_idx) < game->tableau_.size()) {
                auto &source_tableau = game->tableau_[source_tableau_idx];
                if (!source_tableau.empty()) {
                  game->foundation_[0].push_back(source_tableau.back().card);  // Move to discard
                  source_tableau.pop_back();
                  if (!source_tableau.empty() && !source_tableau.back().face_up) {
                    source_tableau.back().face_up = true;
                  }
                }
              }
            }

            // Move waste card to discard
            if (!game->waste_.empty()) {
              game->foundation_[0].push_back(game->waste_.back());  // Move to discard
              game->waste_.pop_back();
            }

            move_successful = true;
            game->playSound(GameSoundEvent::CardPlace);
          }
        }
      }

      if (move_successful) {
        if (game->checkWinCondition()) {
          game->startWinAnimation();
        }
        gtk_widget_queue_draw(game->game_area_);
      }
    }

    game->dragging_ = false;
    game->drag_cards_.clear();
    game->drag_source_pile_ = -1;
    gtk_widget_queue_draw(game->game_area_);
  }

  return TRUE;
}

void PyramidGame::handleStockPileClick() {
  if (stock_.empty()) {
    // Reset: move waste back to stock
    while (!waste_.empty()) {
      stock_.push_back(waste_.back());
      waste_.pop_back();
    }
    refreshDisplay();
  } else {
    if (stock_to_waste_animation_active_) {
      return;
    }
    startStockToWasteAnimation();
  }
}

bool PyramidGame::tryMoveToFoundation(const cardlib::Card &card) {
  // Not used in Pyramid Solitaire
  return false;
}

gboolean PyramidGame::onMotionNotify(GtkWidget *widget, GdkEventMotion *event,
                                       gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  if (game->dragging_) {
    game->drag_start_x_ = event->x;
    game->drag_start_y_ = event->y;
    gtk_widget_queue_draw(game->game_area_);
  }

  return TRUE;
}

std::vector<cardlib::Card> PyramidGame::getDragCards(int pile_index,
                                                     int card_index) {
  int max_foundation_index = 2 + foundation_.size() - 1;
  int first_tableau_index = max_foundation_index + 1;

  // Tableau piles
  if (pile_index >= first_tableau_index) {
    int tableau_idx = pile_index - first_tableau_index;
    if (tableau_idx >= 0 && static_cast<size_t>(tableau_idx) < tableau_.size()) {
      const auto &tableau_pile = tableau_[tableau_idx];
      // Only get the top card (not a sequence like in other solitaire games)
      if (!tableau_pile.empty() && tableau_pile.back().face_up) {
        return {tableau_pile.back().card};
      }
    }
    return std::vector<cardlib::Card>();
  }

  // Waste pile
  if (pile_index == 1) {
    if (!waste_.empty()) {
      return {waste_.back()};
    }
    return std::vector<cardlib::Card>();
  }

  // Stock pile (can't drag from it)
  if (pile_index == 0) {
    return std::vector<cardlib::Card>();
  }

  return std::vector<cardlib::Card>();
}
