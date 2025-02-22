#ifndef PEACHES_H
#define PEACHES_H

#include "cardlib.h"
#include <gtk/gtk.h>
#include <cairo.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <random>
#include <chrono>

enum class PlayerType {
    HUMAN,
    AI_CONSERVATIVE,  // Prefers to wait for better hands
    AI_AGGRESSIVE,    // Knocks earlier, takes more risks
    AI_BALANCED      // Mix of conservative and aggressive
};

struct Player {
    std::string name;
    std::vector<cardlib::Card> hand;
    int tokens;
    bool on_bread_line;
    PlayerType type;
    
    Player(const std::string& n, PlayerType t = PlayerType::HUMAN) 
        : name(n), tokens(4), on_bread_line(false), type(t) {}
};

class ThirtyOneGame {
public:
    ThirtyOneGame();  // Default constructor with 3 AI opponents
    ThirtyOneGame(const std::vector<std::string>& player_names);  // Custom players constructor
    ~ThirtyOneGame();
    
    void run(int argc, char** argv);

private:
    // AI methods
    void performAITurn();
    bool shouldAIKnock(const Player& ai_player) const;
    bool shouldAITakeDiscard(const Player& ai_player) const;
    std::pair<bool, int> getBestDiscardForAI(const Player& ai_player) const;
    int getOptimalSuitForHand(const std::vector<cardlib::Card>& hand) const;
    
    // Timer for AI moves
    static constexpr int AI_MOVE_DELAY_MS = 1000;  // 1 second delay between AI moves
    guint ai_timer_id_;
    static gboolean onAITimer(gpointer data);
    void scheduleNextAIMove();
    
    // Game state
    bool waiting_for_ai_;
    bool game_paused_;
    
    // Random number generator for AI decisions
    mutable std::mt19937 rng_;
    
    // Game components
    cardlib::Deck deck_;
    std::vector<Player> players_;
    std::vector<cardlib::Card> discard_pile_;
    size_t current_player_;
    bool someone_knocked_;
    size_t knock_initiator_;
    
    // GUI elements
    GtkWidget* window_;
    GtkWidget* game_area_;
    GtkWidget* vbox_;
    cairo_surface_t* buffer_surface_;
    cairo_t* buffer_cr_;
    
    // Card rendering
    static constexpr int CARD_WIDTH = 100;
    static constexpr int CARD_HEIGHT = 145;
    static constexpr int CARD_SPACING = 20;
    std::unordered_map<std::string, cairo_surface_t*> card_surface_cache_;
    
    // Drag and drop state
    bool dragging_;
    int drag_source_pile_;  // -1: none, 0: deck, 1: discard, 2: player hand
    int drag_card_index_;
    double drag_start_x_;
    double drag_start_y_;
    double drag_offset_x_;
    double drag_offset_y_;
    cardlib::Card* dragged_card_;
    
    // Methods
    void initializeDefaultGame();
    void showGameState();
    void handleAIMove(const Player& ai_player);
    void startNewRound();
    void nextPlayer();
    void drawStatusArea(cairo_t* cr, int width, int height);
    void drawPlayerInfo(cairo_t* cr, const Player& player, int x, int y, bool is_current);
    void setupWindow();
    void setupGameArea();
    void setupMenuBar();
    void setupAISpeedMenu(GtkWidget* gameMenu);
    void initializeCardCache();
    void cleanupCardCache();
    void drawCard(cairo_t* cr, int x, int y, const cardlib::Card* card, bool face_up);
    void refreshDisplay();
    
    // Event handlers
    static gboolean onDraw(GtkWidget* widget, cairo_t* cr, gpointer data);
    static gboolean onButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean onButtonRelease(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean onMotionNotify(GtkWidget* widget, GdkEventMotion* event, gpointer data);
    static void onNewGame(GtkWidget* widget, gpointer data);
    static void onQuit(GtkWidget* widget, gpointer data);
    static void onAbout(GtkWidget* widget, gpointer data);
    static void onAISpeedChanged(GtkWidget* widget, gpointer data);
    
    // Game actions
    void drawFromDeck();
    void drawFromDiscard();
    void discard(int card_index);
    void knock();
    void layDown();
    void resolveRound();
    void eliminatePlayer(size_t player_index);
    
    // Helper methods
    std::pair<int, int> getPileAt(int x, int y) const;
    bool isValidDragSource(int pile_index, int card_index) const;
    cairo_surface_t* getCardSurface(const cardlib::Card& card);
    cairo_surface_t* getCardBackSurface();
    
    // Game state queries
    bool isGameOver() const;
    int calculateHandValue(const std::vector<cardlib::Card>& hand) const;
    bool hasThreeAces(const std::vector<cardlib::Card>& hand) const;
    bool hasTwoFaceCardsAndAce(const std::vector<cardlib::Card>& hand) const;
    int getBestSuitValue(const std::vector<cardlib::Card>& hand) const;
    
    int ai_speed_ms_;  // Configurable AI speed
};

#endif // PEACHES_H
