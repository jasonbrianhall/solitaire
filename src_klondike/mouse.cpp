#include "solitaire.h"

gboolean SolitaireGame::onButtonPress(GtkWidget *widget, GdkEventButton *event,
                                      gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  game->keyboard_navigation_active_ = false;
  game->keyboard_selection_active_ = false;

if (game->win_animation_active_) {
  game->stopWinAnimation();
  return TRUE;
}

  // If any animation is active, block all interactions
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

    if (pile_index >= 0 && game->isValidDragSource(pile_index, card_index)) {
      game->dragging_ = true;
      game->drag_source_pile_ = pile_index;
      game->drag_start_x_ = event->x;
      game->drag_start_y_ = event->y;
      game->drag_cards_ = game->getDragCards(pile_index, card_index);
      game->playSound(GameSoundEvent::CardFlip);
      // Calculate offsets
      int x_offset_multiplier;
      if (pile_index >= 6) {
        x_offset_multiplier = pile_index - 6;
      } else if (pile_index >= 2 && pile_index <= 5) {
        x_offset_multiplier = pile_index + 1;
      } else if (pile_index == 1) {
        x_offset_multiplier = 1;
      } else {
        x_offset_multiplier = 0;
      }

      game->drag_offset_x_ =
          event->x - (game->current_card_spacing_ +
                      x_offset_multiplier * (game->current_card_width_ +
                                             game->current_card_spacing_));

      if (pile_index >= 6) {
        game->drag_offset_y_ =
            event->y -
            (game->current_card_spacing_ + game->current_card_height_ +
             game->current_vert_spacing_ +
             card_index * game->current_vert_spacing_);
      } else {
        game->drag_offset_y_ = event->y - game->current_card_spacing_;
      }
    }
  } else if (event->button == 3) { // Right click
    auto [pile_index, card_index] = game->getPileAt(event->x, event->y);

    // Try to move card to foundation
    if (pile_index >= 0) {
      const cardlib::Card *card = nullptr;
      int target_foundation = -1;

      // Get the card based on pile type
      if (pile_index == 1 && !game->waste_.empty()) {
        card = &game->waste_.back();
        // Find which foundation to move to
        for (int i = 0; i < game->foundation_.size(); i++) {
          if (game->canMoveToFoundation(*card, i)) {
            target_foundation = i;
            break;
          }
        }

        if (target_foundation >= 0) {
          // Start animation
          game->startFoundationMoveAnimation(*card, pile_index, 0,
                                             target_foundation + 2);
          game->playSound(GameSoundEvent::CardPlace);
          // Remove card from waste pile
          game->waste_.pop_back();

          // The card will be added to the foundation in
          // updateFoundationMoveAnimation when animation completes
          return TRUE;
        }
      } else if (pile_index >= 6 && pile_index <= 12) {
        auto &tableau_pile = game->tableau_[pile_index - 6];
        if (!tableau_pile.empty() && tableau_pile.back().face_up) {
          card = &tableau_pile.back().card;

          // Find which foundation to move to
          for (int i = 0; i < game->foundation_.size(); i++) {
            if (game->canMoveToFoundation(*card, i)) {
              target_foundation = i;
              break;
            }
          }

          if (target_foundation >= 0) {
            // Start animation
            game->startFoundationMoveAnimation(*card, pile_index,
                                               tableau_pile.size() - 1,
                                               target_foundation + 2);
            game->playSound(GameSoundEvent::CardPlace);

            // Remove card from tableau
            tableau_pile.pop_back();

            // Flip new top card if needed
            if (!tableau_pile.empty() && !tableau_pile.back().face_up) {
              // game->playSound(GameSoundEvent::CardFlip);

              tableau_pile.back().face_up = true;
            }

            // The card will be added to the foundation in
            // updateFoundationMoveAnimation when animation completes
            return TRUE;
          }
        }
      }
    }

    return TRUE;
  }

  return TRUE;
}

gboolean SolitaireGame::onButtonRelease(GtkWidget *widget,
                                        GdkEventButton *event, gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);
  game->keyboard_navigation_active_ = false;

  if (event->button == 1 && game->dragging_) {
    auto [target_pile, card_index] = game->getPileAt(event->x, event->y);

    if (target_pile >= 0) {
      bool move_successful = false;

      // Calculate max foundation index based on actual foundation size
      int max_foundation_index = 2 + game->foundation_.size() - 1;
      
      // Calculate max tableau index
      int first_tableau_index = 6; // Tableau piles always start at index 6
      int max_tableau_index = first_tableau_index + game->tableau_.size() - 1;

      // Handle dropping on foundation piles (all foundation piles, not just the first four)
      if (target_pile >= 2 && target_pile <= max_foundation_index) {
        auto &foundation_pile = game->foundation_[target_pile - 2];
        if (game->canMoveToPile(game->drag_cards_, foundation_pile, true)) {
          // Remove card from source
          if (game->drag_source_pile_ >= first_tableau_index && 
              game->drag_source_pile_ <= max_tableau_index) {
            auto &source_tableau = game->tableau_[game->drag_source_pile_ - first_tableau_index];
            source_tableau.pop_back();

            // Flip over the new top card if there is one
            if (!source_tableau.empty() && !source_tableau.back().face_up) {
              source_tableau.back().face_up = true;
            }
          } else {
            auto &source = game->getPileReference(game->drag_source_pile_);
            source.pop_back();
          }

          // Add to foundation
          foundation_pile.push_back(game->drag_cards_[0]);
          move_successful = true;
        }
      }
      // Handle dropping on tableau piles
      else if (target_pile >= first_tableau_index && target_pile <= max_tableau_index) {
        auto &tableau_pile = game->tableau_[target_pile - first_tableau_index];
        std::vector<cardlib::Card> target_cards;
        if (!tableau_pile.empty()) {
          target_cards = {tableau_pile.back().card};
        }

        if (game->canMoveToPile(game->drag_cards_, target_cards, false)) {
          // Remove cards from source
          if (game->drag_source_pile_ >= first_tableau_index && 
              game->drag_source_pile_ <= max_tableau_index) {
            auto &source_tableau = game->tableau_[game->drag_source_pile_ - first_tableau_index];
            source_tableau.erase(source_tableau.end() -
                                     game->drag_cards_.size(),
                                 source_tableau.end());

            // Flip over the new top card if there is one
            if (!source_tableau.empty() && !source_tableau.back().face_up) {
              source_tableau.back().face_up = true;
            }
          } else {
            auto &source = game->getPileReference(game->drag_source_pile_);
            source.erase(source.end() - game->drag_cards_.size(), source.end());
          }

          // Add cards to target tableau
          for (const auto &card : game->drag_cards_) {
            tableau_pile.emplace_back(card, true);
          }
          move_successful = true;
          game->playSound(GameSoundEvent::CardPlace);
        }
      }

      if (move_successful) {
        if (game->checkWinCondition()) {
          game->startWinAnimation(); // Start animation instead of showing dialog
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

void SolitaireGame::handleStockPileClick() {
  if (stock_.empty()) {
    // If stock is empty, move all waste cards back to stock in reverse order
    while (!waste_.empty()) {
      stock_.push_back(waste_.back());
      waste_.pop_back();
    }
    refreshDisplay();
  } else {
    // Don't start a new animation if one is already running
    if (stock_to_waste_animation_active_) {
      return;
    }

    // Start animation instead of immediately moving cards
    startStockToWasteAnimation();
  }
}

bool SolitaireGame::tryMoveToFoundation(const cardlib::Card &card) {
  // Try each foundation pile
  for (size_t i = 0; i < foundation_.size(); i++) {
    std::vector<cardlib::Card> cards = {card};
    if (canMoveToPile(cards, foundation_[i], true)) {
      foundation_[i].push_back(card);
      return true;
    }
  }
  return false;
}

gboolean SolitaireGame::onMotionNotify(GtkWidget *widget, GdkEventMotion *event,
                                       gpointer data) {
  SolitaireGame *game = static_cast<SolitaireGame *>(data);

  if (game->dragging_) {
    game->drag_start_x_ = event->x;
    game->drag_start_y_ = event->y;
    gtk_widget_queue_draw(game->game_area_);
  }

  return TRUE;
}

std::vector<cardlib::Card> SolitaireGame::getDragCards(int pile_index,
                                                       int card_index) {
  if (pile_index >= 6 && pile_index <= 12) {
    // Handle tableau piles
    const auto &tableau_pile = tableau_[pile_index - 6];
    if (card_index >= 0 &&
        static_cast<size_t>(card_index) < tableau_pile.size() &&
        tableau_pile[card_index].face_up) {
      return getTableauCardsAsCards(tableau_pile, card_index);
    }
    return std::vector<cardlib::Card>();
  }

  // Handle other piles using getPileReference
  auto &pile = getPileReference(pile_index);
  if (card_index >= 0 && static_cast<size_t>(card_index) < pile.size()) {
    return std::vector<cardlib::Card>(pile.begin() + card_index, pile.end());
  }
  return std::vector<cardlib::Card>();
}
