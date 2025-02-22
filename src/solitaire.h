#ifndef SOLITAIRE_H
#define SOLITAIRE_H

#include "cardlib.h"
#include <gtk/gtk.h>
#include <vector>
#include <memory>
#include <optional>

struct TableauCard {
    cardlib::Card card;
    bool face_up;
    
    TableauCard(const cardlib::Card& c, bool up) : card(c), face_up(up) {}
};

class SolitaireGame {
public:
    SolitaireGame();
    ~SolitaireGame();

    void run(int argc, char** argv);

private:
    // Game state
    cardlib::Deck deck_;
    std::vector<cardlib::Card> stock_;          // Draw pile
    std::vector<cardlib::Card> waste_;          // Faced-up cards from stock
    std::vector<std::vector<cardlib::Card>> foundation_; // 4 piles for aces
    std::vector<std::vector<TableauCard>> tableau_;

    // Helper function to convert TableauCard vector to Card vector
    std::vector<cardlib::Card> getTableauCardsAsCards(const std::vector<TableauCard>& tableau_cards, int start_index);

    // Method to get cards for dragging
    std::vector<cardlib::Card> getDragCards(int pile_index, int card_index);

    void drawCard(cairo_t* cr, int x, int y, const cardlib::CardImage* img) const;
    void flipTopTableauCard(int);
    // Drag and drop state
    bool dragging_;
    GtkWidget* drag_source_;
    std::vector<cardlib::Card> drag_cards_;
    int drag_source_pile_;
    int drag_start_x_, drag_start_y_;

    // GTK widgets
    GtkWidget* window_;
    GtkWidget* game_area_;
    std::vector<GtkWidget*> card_widgets_;

    // Card dimensions and spacing
    static constexpr int CARD_WIDTH = 100;
    static constexpr int CARD_HEIGHT = 145;
    static constexpr int CARD_SPACING = 20;
    static constexpr int VERT_SPACING = 30;

    // Initialize game
    void initializeGame();
    void deal();

    // UI setup
    void setupWindow();
    void setupGameArea();
    GtkWidget* createCardWidget(const cardlib::Card& card, bool face_up);

    // Game logic
    bool canMoveToPile(const std::vector<cardlib::Card>& cards, const std::vector<cardlib::Card>& target) const;
    bool canMoveToFoundation(const cardlib::Card& card, int foundation_index) const;
    void moveCards(std::vector<cardlib::Card>& from, std::vector<cardlib::Card>& to, size_t count);
    bool checkWinCondition() const;

    // Event handlers
    static gboolean onDraw(GtkWidget* widget, cairo_t* cr, gpointer data);
    static gboolean onButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean onButtonRelease(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean onMotionNotify(GtkWidget* widget, GdkEventMotion* event, gpointer data);

    // Helper functions
    std::pair<int, int> getPileAt(int x, int y) const;
    void refreshDisplay();
    std::vector<cardlib::Card>& getPileReference(int pile_index);
    bool isValidDragSource(int pile_index, int card_index) const;
};

#endif // SOLITAIRE_H
