#include "freecell.h"
#include <gtk/gtk.h>

void FreecellGame::startNoMovesTimer() {
  // Stop any existing timer first
  stopNoMovesTimer();
  
  // Start a new timer that runs every second
  no_moves_timer_id_ = g_timeout_add_seconds(1, checkNoMoves, this);
}

void FreecellGame::stopNoMovesTimer() {
  if (no_moves_timer_id_ > 0) {
    g_source_remove(no_moves_timer_id_);
    no_moves_timer_id_ = 0;
  }
}

gboolean FreecellGame::checkNoMoves(gpointer data) {
  FreecellGame *game = static_cast<FreecellGame *>(data);
  
  // Don't run checks during animations
  if (game->win_animation_active_ || 
      game->foundation_move_animation_active_ || 
      game->deal_animation_active_ ||
      game->auto_finish_active_) {
    return TRUE;
  }
  
  // Check if any moves are possible
  if (!game->areAnyMovesPossible()) {
    // Stop the timer
    game->stopNoMovesTimer();
    
    // Show dialog on the main thread
    gdk_threads_add_idle(
      +[](gpointer user_data) -> gboolean {
        FreecellGame *game = static_cast<FreecellGame *>(user_data);
        
        // Create dialog
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(game->window_), 
            GTK_DIALOG_MODAL, 
            GTK_MESSAGE_INFO, 
            GTK_BUTTONS_OK, 
            "No more moves possible!\n\nTry a different strategy or start a new game.");
        
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        return G_SOURCE_REMOVE;
      }, 
      game
    );
    
    return G_SOURCE_REMOVE; // Stop the timer
  }
  
  return TRUE;
}

bool FreecellGame::areAnyMovesPossible() const {
  // Check if any card can move to a foundation
  for (const auto &freecell : freecells_) {
    if (freecell.has_value()) {
      for (int i = 0; i < 4; i++) {
        if (const_cast<FreecellGame*>(this)->canMoveToFoundation(freecell.value(), i)) {
          return true;
        }
      }
    }
  }
  
  // Check tableau piles for foundation moves
  for (const auto &tableau_pile : tableau_) {
    if (!tableau_pile.empty()) {
      const cardlib::Card &top_card = tableau_pile.back();
      for (int i = 0; i < 4; i++) {
        if (const_cast<FreecellGame*>(this)->canMoveToFoundation(top_card, i)) {
          return true;
        }
      }
    }
  }
  
  // Check moves between tableau piles
  for (size_t source_idx = 0; source_idx < tableau_.size(); source_idx++) {
    if (tableau_[source_idx].empty()) continue;
    
    const cardlib::Card &source_card = tableau_[source_idx].back();
    
    // Check empty tableau piles
    for (size_t dest_idx = 0; dest_idx < tableau_.size(); dest_idx++) {
      if (source_idx == dest_idx) continue;
      
      if (tableau_[dest_idx].empty() || 
          const_cast<FreecellGame*>(this)->canMoveToTableau(source_card, dest_idx)) {
        return true;
      }
      
      // Check multi-card move if a valid stack
      std::vector<cardlib::Card> stack;
      for (int i = tableau_[source_idx].size() - 1; i >= 0; i--) {
        stack.insert(stack.begin(), tableau_[source_idx][i]);
        if (const_cast<FreecellGame*>(this)->canMoveTableauStack(stack, dest_idx)) {
          return true;
        }
      }
    }
  }
  
  // Check moves to empty freecells
  int empty_freecells = 0;
  for (const auto &cell : freecells_) {
    if (!cell.has_value()) {
      empty_freecells++;
    }
  }
  
  // If we have empty freecells and tableau has movable cards
  if (empty_freecells > 0) {
    for (const auto &tableau_pile : tableau_) {
      if (!tableau_pile.empty()) {
        return true;
      }
    }
  }
  
  return false;
}
