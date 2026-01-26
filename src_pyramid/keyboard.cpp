#include "pyramid.h"
#include <iostream>

// Pyramid Solitaire keyboard navigation
// Allows players to navigate and select cards using keyboard

gboolean PyramidGame::onKeyPress(GtkWidget *widget, GdkEventKey *event,
                                  gpointer data) {
  PyramidGame *game = static_cast<PyramidGame *>(data);

  // Handle common keys
  switch (event->keyval) {
    case GDK_KEY_Escape: {
      // Deselect current card
      game->selected_card_row_ = -1;
      game->selected_card_col_ = -1;
      game->card_selected_ = false;
      game->refreshDisplay();
      return TRUE;
    }

    case GDK_KEY_F11: {
      // Toggle fullscreen
      game->toggleFullscreen();
      return TRUE;
    }

    case GDK_KEY_n:
    case GDK_KEY_N: {
      // New game
      if (event->state & GDK_CONTROL_MASK) {
        game->restartGame();
        return TRUE;
      }
      break;
    }

    case GDK_KEY_q:
    case GDK_KEY_Q: {
      // Quit
      if (event->state & GDK_CONTROL_MASK) {
        gtk_widget_destroy(game->window_);
        return TRUE;
      }
      break;
    }

    case GDK_KEY_h:
    case GDK_KEY_H: {
      // Help
      if (event->state & GDK_CONTROL_MASK) {
        game->showHowToPlay();
        return TRUE;
      }
      break;
    }

    case GDK_KEY_Up:
    case GDK_KEY_Down:
    case GDK_KEY_Left:
    case GDK_KEY_Right: {
      // Navigate pyramid with arrow keys
      if (!game->keyboard_navigation_active_) {
        game->keyboard_navigation_active_ = true;
        // Start at bottom-left
        game->selected_pile_ = 20; // Row 6, col 0
      }

      // Calculate current position
      int row = game->selected_pile_ / 10;
      int col = game->selected_pile_ % 10;

      if (event->keyval == GDK_KEY_Left) {
        col = std::max(0, col - 1);
      } else if (event->keyval == GDK_KEY_Right) {
        col = std::min(row, col + 1);
      } else if (event->keyval == GDK_KEY_Up) {
        row = std::max(0, row - 1);
        col = std::min(col, row);
      } else if (event->keyval == GDK_KEY_Down) {
        row = std::min(6, row + 1);
        col = std::min(col, row);
      }

      game->selected_pile_ = row * 10 + col;
      game->refreshDisplay();
      return TRUE;
    }

    case GDK_KEY_Return:
    case GDK_KEY_space: {
      // Select/activate card
      if (game->keyboard_navigation_active_) {
        int row = game->selected_pile_ / 10;
        int col = game->selected_pile_ % 10;

        if (row >= 0 && row < 7 && col >= 0 && col <= row) {
          game->selectPyramidCard(row, col);
        }
      }
      return TRUE;
    }

    case GDK_KEY_s:
    case GDK_KEY_S: {
      // Stock pile (draw card)
      if (event->state & GDK_CONTROL_MASK) {
        game->handleStockPileClick();
        return TRUE;
      }
      break;
    }

    default:
      break;
  }

  return FALSE;
}

// ============================================================================
// KEYBOARD NAVIGATION HELPER METHODS
// ============================================================================

void PyramidGame::selectNextCard() {
  if (!keyboard_navigation_active_) {
    return;
  }

  // Move to next exposed card
  int row = selected_pile_ / 10;
  int col = selected_pile_ % 10;

  // Try moving right
  if (col < row) {
    col++;
  } else if (row < 6) {
    // Move to next row
    row++;
    col = 0;
  } else {
    // Wrap to beginning
    row = 0;
    col = 0;
  }

  // Ensure we're in valid position
  if (row > 6) {
    row = 6;
    col = 6;
  }
  if (col > row) {
    col = row;
  }

  selected_pile_ = row * 10 + col;
  refreshDisplay();
}

void PyramidGame::selectPreviousCard() {
  if (!keyboard_navigation_active_) {
    return;
  }

  // Move to previous exposed card
  int row = selected_pile_ / 10;
  int col = selected_pile_ % 10;

  // Try moving left
  if (col > 0) {
    col--;
  } else if (row > 0) {
    // Move to previous row
    row--;
    col = row;
  } else {
    // Wrap to end
    row = 6;
    col = 6;
  }

  selected_pile_ = row * 10 + col;
  refreshDisplay();
}

void PyramidGame::activateSelected() {
  if (!keyboard_navigation_active_) {
    return;
  }

  int row = selected_pile_ / 10;
  int col = selected_pile_ % 10;

  if (row >= 0 && row < 7 && col >= 0 && col <= row) {
    selectPyramidCard(row, col);
  }
}

void PyramidGame::resetKeyboardNavigation() {
  keyboard_navigation_active_ = false;
  keyboard_selection_active_ = false;
  selected_pile_ = -1;
  selected_card_idx_ = -1;
}
