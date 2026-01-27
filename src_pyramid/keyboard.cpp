#include "pyramid.h"
#include <gtk/gtk.h>

// Pyramid Solitaire - Keyboard input handling
// Arrow keys navigate cards, Enter to pair/remove, Space for stock

gboolean PyramidGame::onKeyPress(GtkWidget *widget, GdkEventKey *event,
                                   gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  if (game->win_animation_active_) {
    game->stopWinAnimation();
    return TRUE;
  }

  bool ctrl_pressed = (event->state & GDK_CONTROL_MASK);

  // Block keyboard during animations
  if (game->foundation_move_animation_active_ ||
      game->stock_to_waste_animation_active_ || game->deal_animation_active_ ||
      game->win_animation_active_) {
    if (event->keyval == GDK_KEY_Escape) {
      return TRUE;
    }
    return FALSE;
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
      game->refreshDisplay();
      return TRUE;
    }
    // Space/S: Draw from stock
    if (!ctrl_pressed) {
      game->handleStockPileClick();
      return TRUE;
    }
    break;

  case GDK_KEY_space:
    // Draw from stock
    game->handleStockPileClick();
    return TRUE;

  case GDK_KEY_Up:
  case GDK_KEY_Down:
  case GDK_KEY_Left:
  case GDK_KEY_Right:
    // Pyramid keyboard navigation would go here
    // For now, navigation is mouse-only
    return TRUE;

  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:
    // Enter key: attempt to auto-pair selected cards
    game->autoFinishGame();
    return TRUE;

  case GDK_KEY_o:
  case GDK_KEY_O:
    if (ctrl_pressed) {
      // CTRL+O: Load test layout with one King (for easy testing)
      game->dealTestLayout();
      game->refreshDisplay();
      return TRUE;
    }
    break;

  default:
    break;
  }

  return FALSE;
}

void PyramidGame::toggleFullscreen() {
  if (is_fullscreen_) {
    gtk_window_unfullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = false;
  } else {
    gtk_window_fullscreen(GTK_WINDOW(window_));
    is_fullscreen_ = true;
  }
}

void PyramidGame::resetKeyboardNavigation() {
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;
  source_pile_ = -1;
  source_card_idx_ = -1;
  selected_pile_ = -1;
  selected_card_idx_ = -1;
}
