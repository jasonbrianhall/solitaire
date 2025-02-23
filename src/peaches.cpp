#include "peaches.h"
#include <algorithm>
#include <stdexcept>

ThirtyOneGame::ThirtyOneGame() 
    : waiting_for_ai_(false), game_paused_(false), ai_timer_id_(0),
      dragging_(false), drag_source_pile_(-1), drag_card_index_(-1),
      window_(nullptr), game_area_(nullptr), buffer_surface_(nullptr),
      buffer_cr_(nullptr), ai_speed_ms_(AI_MOVE_DELAY_MS),
      has_drawn_card_(false) {
    
    rng_.seed(std::chrono::system_clock::now().time_since_epoch().count());
    
    // Initialize with human player and AI players with personalized names
    players_.emplace_back("Player", PlayerType::HUMAN);
    players_.emplace_back("Ellie", PlayerType::AI_CONSERVATIVE);
    players_.emplace_back("Ariel", PlayerType::AI_CONSERVATIVE);
    players_.emplace_back("Skylar", PlayerType::AI_CONSERVATIVE);

    //players_.emplace_back("Ariel", PlayerType::AI_AGGRESSIVE);
    //players_.emplace_back("Skylar", PlayerType::AI_BALANCED);
    
    try {
        deck_ = cardlib::Deck("cards.zip");
        deck_.removeJokers();
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load cards.zip: " + std::string(e.what()));
    }
    
    startNewRound();
}

ThirtyOneGame::ThirtyOneGame(const std::vector<std::string>& player_names) 
    : waiting_for_ai_(false), game_paused_(false), ai_timer_id_(0),
      dragging_(false), drag_source_pile_(-1), drag_card_index_(-1),
      window_(nullptr), game_area_(nullptr), buffer_surface_(nullptr),
      buffer_cr_(nullptr), ai_speed_ms_(AI_MOVE_DELAY_MS) {
          
    if (player_names.size() < 2) {
        throw std::invalid_argument("Need at least 2 players");
    }
    
    for (const auto& name : player_names) {
        players_.emplace_back(name);
    }
    
    try {
        deck_ = cardlib::Deck("cards.zip");
        deck_.removeJokers();
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load cards.zip: " + std::string(e.what()));
    }
    
    startNewRound();
}

ThirtyOneGame::~ThirtyOneGame() {
    cleanupCardCache();
    if (buffer_cr_) {
        cairo_destroy(buffer_cr_);
    }
    if (buffer_surface_) {
        cairo_surface_destroy(buffer_surface_);
    }
}

void ThirtyOneGame::run(int argc, char** argv) {
    gtk_init(&argc, &argv);
    setupWindow();
    setupGameArea();
    gtk_main();
}

void ThirtyOneGame::startNewRound() {
    has_drawn_card_ = false;
    deck_.reset();
    deck_.shuffle();
    
    discard_pile_.clear();
    for (auto& player : players_) {
        player.hand.clear();
    }
    
    // Deal 3 cards to each player
    for (int i = 0; i < 3; ++i) {
        for (auto& player : players_) {
            if (auto card = deck_.drawCard()) {
                player.hand.push_back(*card);
            }
        }
    }
    
    // Start discard pile
    if (auto card = deck_.drawCard()) {
        discard_pile_.push_back(*card);
    }
    
    someone_knocked_ = false;
    current_player_ = 0;
    knock_initiator_ = 0;
    
    refreshDisplay();
}

int ThirtyOneGame::calculateHandValue(const std::vector<cardlib::Card>& hand) const {
    return getBestSuitValue(hand);
}

int ThirtyOneGame::getBestSuitValue(const std::vector<cardlib::Card>& hand) const {
    std::vector<int> suit_values(4, 0);
    
    for (const auto& card : hand) {
        int suit_index = static_cast<int>(card.suit);
        if (suit_index >= 0 && suit_index < 4) {
            int value;
            if (card.rank == cardlib::Rank::ACE) {
                value = 11;
            } else if (card.rank == cardlib::Rank::KING || 
                      card.rank == cardlib::Rank::QUEEN ||
                      card.rank == cardlib::Rank::JACK || 
                      card.rank == cardlib::Rank::TEN) {
                value = 10;
            } else {
                value = static_cast<int>(card.rank);
            }
            suit_values[suit_index] += value;
        }
    }
    
    return *std::max_element(suit_values.begin(), suit_values.end());
}

bool ThirtyOneGame::hasThreeAces(const std::vector<cardlib::Card>& hand) const {
    int ace_count = 0;
    for (const auto& card : hand) {
        if (card.rank == cardlib::Rank::ACE) {
            ace_count++;
        }
    }
    return ace_count >= 3;
}

bool ThirtyOneGame::hasTwoFaceCardsAndAce(const std::vector<cardlib::Card>& hand) const {
    int face_count = 0;
    int ace_count = 0;
    
    for (const auto& card : hand) {
        if (card.rank == cardlib::Rank::ACE) {
            ace_count++;
        } else if (card.rank == cardlib::Rank::KING || 
                   card.rank == cardlib::Rank::QUEEN ||
                   card.rank == cardlib::Rank::JACK ||
                   card.rank == cardlib::Rank::TEN ) {

            face_count++;
        }
    }
    
    return face_count >= 2 && ace_count >= 1;
}

void ThirtyOneGame::drawFromDeck() {
    if (!has_drawn_card_) {
        if (auto card = deck_.drawCard()) {
            players_[current_player_].hand.push_back(*card);
            has_drawn_card_ = true;
            refreshDisplay();
        }
    }
}

void ThirtyOneGame::drawFromDiscard() {
    if (!has_drawn_card_ && !discard_pile_.empty()) {
        players_[current_player_].hand.push_back(discard_pile_.back());
        discard_pile_.pop_back();
        has_drawn_card_ = true;
        refreshDisplay();
    }
}

void ThirtyOneGame::discard(int card_index) {
    if (!has_drawn_card_) return;  // Must draw before discarding
    
    auto& current_hand = players_[current_player_].hand;
    if (card_index >= 0 && static_cast<size_t>(card_index) < current_hand.size()) {
        discard_pile_.push_back(current_hand[card_index]);
        current_hand.erase(current_hand.begin() + card_index);
        has_drawn_card_ = false;  // Reset for next player
        nextPlayer();
    }
}

void ThirtyOneGame::knock() {
    if (!someone_knocked_) {
        someone_knocked_ = true;
        knock_initiator_ = current_player_;
    }
    nextPlayer();
    
    if (current_player_ == knock_initiator_) {
        resolveRound();
    }
}

void ThirtyOneGame::layDown() {
    auto& current_hand = players_[current_player_].hand;
    
    if (hasThreeAces(current_hand)) {
        for (size_t i = 0; i < players_.size(); ++i) {
            if (i != current_player_) {
                players_[i].tokens = std::max(0, players_[i].tokens - 2);
                if (players_[i].tokens == 0 && !players_[i].on_bread_line) {
                    players_[i].on_bread_line = true;
                } else if (players_[i].tokens == 0 && players_[i].on_bread_line) {
                    eliminatePlayer(i);
                }
            }
        }
    } else if (calculateHandValue(current_hand) == 31 || 
               hasTwoFaceCardsAndAce(current_hand)) {
        for (size_t i = 0; i < players_.size(); ++i) {
            if (i != current_player_) {
                players_[i].tokens = std::max(0, players_[i].tokens - 1);
                if (players_[i].tokens == 0 && !players_[i].on_bread_line) {
                    players_[i].on_bread_line = true;
                } else if (players_[i].tokens == 0 && players_[i].on_bread_line) {
                    eliminatePlayer(i);
                }
            }
        }
    }
    
    startNewRound();
}

void ThirtyOneGame::performAITurn() {
    Player& ai_player = players_[current_player_];
    if (ai_player.type == PlayerType::HUMAN) return;
    
    int current_value = calculateHandValue(ai_player.hand);
    printf("Current value is %i\n", current_value);
    
    // Check for special combinations first
    printf("Checking if player has 31 or 33\n");
    if (hasThreeAces(ai_player.hand) || 
        hasTwoFaceCardsAndAce(ai_player.hand) || 
        current_value == 31) {
        layDown();
        return;
    }
    
    // Check if we should knock
    printf("Checking if player should knock\n");
    if (shouldAIKnock(ai_player)) {
        knock();
        return;
    }
    
    // Draw a card if we haven't yet
    printf("Drawing a Card\n");
    if (!has_drawn_card_) {
        if (!discard_pile_.empty() && shouldAITakeDiscard(ai_player)) {
            drawFromDiscard();
        } else {
            drawFromDeck();
        }
    }
    
    // Always discard after drawing
    printf("Discarding Card\n");
    auto [should_discard, discard_index] = getBestDiscardForAI(ai_player);
    if (should_discard) {
        discard(discard_index);
    }
    
    refreshDisplay();
    // Don't set waiting_for_ai_ to false here anymore
}

bool ThirtyOneGame::shouldAIKnock(const Player& ai_player) const {
    int value = calculateHandValue(ai_player.hand);
    
    switch (ai_player.type) {
        case PlayerType::AI_CONSERVATIVE:
            return value >= 27;
            
        case PlayerType::AI_AGGRESSIVE:
            return value >= 21;
            
        case PlayerType::AI_BALANCED:
            return value >= 24;
            
        default:
            return false;
    }
}

bool ThirtyOneGame::shouldAITakeDiscard(const Player& ai_player) const {
    if (discard_pile_.empty()) return false;
    
    const cardlib::Card& top_discard = discard_pile_.back();
    int optimal_suit = getOptimalSuitForHand(ai_player.hand);
    
    int same_suit_count = 0;
    int suit_value = 0;
    for (const auto& card : ai_player.hand) {
        if (card.suit == top_discard.suit) {
            same_suit_count++;
            suit_value += (card.rank == cardlib::Rank::ACE) ? 11 :
                         (static_cast<int>(card.rank) >= 10) ? 10 :
                         static_cast<int>(card.rank);
        }
    }
    
    int card_value = (top_discard.rank == cardlib::Rank::ACE) ? 11 :
                    (static_cast<int>(top_discard.rank) >= 10) ? 10 :
                    static_cast<int>(top_discard.rank);
    
    switch (ai_player.type) {
        case PlayerType::AI_CONSERVATIVE:
            return same_suit_count >= 2 && 
                   top_discard.suit == static_cast<cardlib::Suit>(optimal_suit);
            
        case PlayerType::AI_AGGRESSIVE:
            return same_suit_count >= 1 || card_value >= 10;
            
        case PlayerType::AI_BALANCED:
            return same_suit_count >= 1 && 
                   (card_value >= 8 || top_discard.suit == static_cast<cardlib::Suit>(optimal_suit));
            
        default:
            return false;
    }
}

std::pair<bool, int> ThirtyOneGame::getBestDiscardForAI(const Player& ai_player) const {
    int optimal_suit = getOptimalSuitForHand(ai_player.hand);
    int worst_card_index = -1;
    int worst_card_value = 32;
    
    for (size_t i = 0; i < ai_player.hand.size(); i++) {
        const auto& card = ai_player.hand[i];
        int card_value = (card.rank == cardlib::Rank::ACE) ? 11 :
                        (static_cast<int>(card.rank) >= 10) ? 10 :
                        static_cast<int>(card.rank);
                        
        if (static_cast<int>(card.suit) != optimal_suit) {
            card_value /= 2;
        }
        
        if (card_value < worst_card_value) {
            worst_card_value = card_value;
            worst_card_index = i;
        }
    }
    
    return {true, worst_card_index};
}

int ThirtyOneGame::getOptimalSuitForHand(const std::vector<cardlib::Card>& hand) const {
    std::vector<int> suit_values(4, 0);
    std::vector<int> suit_counts(4, 0);
    
    for (const auto& card : hand) {
        int suit_index = static_cast<int>(card.suit);
        suit_counts[suit_index]++;
        
        int value = (card.rank == cardlib::Rank::ACE) ? 11 :
                   (static_cast<int>(card.rank) >= 10) ? 10 :
                   static_cast<int>(card.rank);
        suit_values[suit_index] += value;
    }
    
    int best_suit = 0;
    int best_value = 0;
    
    for (int i = 0; i < 4; i++) {
        if (suit_values[i] > best_value || 
            (suit_values[i] == best_value && suit_counts[i] > suit_counts[best_suit])) {
            best_value = suit_values[i];
            best_suit = i;
        }
    }
    
    return best_suit;
}

void ThirtyOneGame::nextPlayer() {
    has_drawn_card_ = false;  // Reset for next player
    do {
        current_player_ = (current_player_ + 1) % players_.size();
    } while (players_[current_player_].tokens < 0);
    
    if (players_[current_player_].type != PlayerType::HUMAN) {
        waiting_for_ai_ = true;
        if (ai_timer_id_ > 0) {
            g_source_remove(ai_timer_id_);
        }
        ai_timer_id_ = g_timeout_add(ai_speed_ms_, onAITimer, this);
    } else {
        waiting_for_ai_ = false;
    }
    
    refreshDisplay();
}

void ThirtyOneGame::resolveRound() {
    int lowest_score = 31;
    std::vector<size_t> lowest_players;
    
    for (size_t i = 0; i < players_.size(); ++i) {
        if (players_[i].tokens >= 0) {
            int score = calculateHandValue(players_[i].hand);
            if (score < lowest_score) {
                lowest_score = score;
                lowest_players.clear();
                lowest_players.push_back(i);
            } else if (score == lowest_score) {
                lowest_players.push_back(i);
            }
        }
    }
    
    if (lowest_players.size() == 1 && lowest_players[0] == knock_initiator_) {
        players_[knock_initiator_].tokens--;
        if (players_[knock_initiator_].tokens == 0 && 
            !players_[knock_initiator_].on_bread_line) {
            players_[knock_initiator_].on_bread_line = true;
        } else if (players_[knock_initiator_].tokens == 0 && 
                   players_[knock_initiator_].on_bread_line) {
            eliminatePlayer(knock_initiator_);
        }
    } else {
        for (size_t player_index : lowest_players) {
            if (player_index != knock_initiator_) {
                players_[player_index].tokens--;
                if (players_[player_index].tokens == 0 && 
                    !players_[player_index].on_bread_line) {
                    players_[player_index].on_bread_line = true;
                } else if (players_[player_index].tokens == 0 && 
                          players_[player_index].on_bread_line) {
                    eliminatePlayer(player_index);
                }
            }
        }
    }
    
    startNewRound();
}

void ThirtyOneGame::eliminatePlayer(size_t player_index) {
    players_[player_index].tokens = -1;
}

bool ThirtyOneGame::isGameOver() const {
    int active_players = 0;
    for (const auto& player : players_) {
        if (player.tokens >= 0) {
            active_players++;
        }
    }
    return active_players <= 1;
}

void ThirtyOneGame::setupWindow() {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "Thirty One (Peaches/Knock)");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1024, 768);
    g_signal_connect(G_OBJECT(window_), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    vbox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), vbox_);

    setupMenuBar();
}

void ThirtyOneGame::setupGameArea() {
    game_area_ = gtk_drawing_area_new();
    gtk_box_pack_start(GTK_BOX(vbox_), game_area_, TRUE, TRUE, 0);

    gtk_widget_add_events(game_area_, 
        GDK_BUTTON_PRESS_MASK | 
        GDK_BUTTON_RELEASE_MASK | 
        GDK_POINTER_MOTION_MASK);

    g_signal_connect(G_OBJECT(game_area_), "draw", G_CALLBACK(onDraw), this);
    g_signal_connect(G_OBJECT(game_area_), "button-press-event", G_CALLBACK(onButtonPress), this);
    g_signal_connect(G_OBJECT(game_area_), "button-release-event", G_CALLBACK(onButtonRelease), this);
    g_signal_connect(G_OBJECT(game_area_), "motion-notify-event", G_CALLBACK(onMotionNotify), this);

    gtk_widget_show_all(window_);
}

void ThirtyOneGame::drawCard(cairo_t* cr, int x, int y, const cardlib::Card* card, bool face_up) {
    if (face_up && card) {
        std::string key = std::to_string(static_cast<int>(card->suit)) + 
                         std::to_string(static_cast<int>(card->rank));
        auto it = card_surface_cache_.find(key);
        
        if (it == card_surface_cache_.end()) {
            if (auto img = deck_.getCardImage(*card)) {
                GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
                gdk_pixbuf_loader_write(loader, img->data.data(), img->data.size(), nullptr);
                gdk_pixbuf_loader_close(loader, nullptr);

                GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
                GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, CARD_WIDTH, CARD_HEIGHT, GDK_INTERP_BILINEAR);

                cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, CARD_WIDTH, CARD_HEIGHT);
                cairo_t* surface_cr = cairo_create(surface);
                gdk_cairo_set_source_pixbuf(surface_cr, scaled, 0, 0);
                cairo_paint(surface_cr);
                cairo_destroy(surface_cr);

                card_surface_cache_[key] = surface;
                it = card_surface_cache_.find(key);

                g_object_unref(scaled);
                g_object_unref(loader);
            }
        }

        if (it != card_surface_cache_.end()) {
            cairo_set_source_surface(cr, it->second, x, y);
            cairo_paint(cr);
        }
    } else {
        auto back_it = card_surface_cache_.find("back");
        if (back_it == card_surface_cache_.end()) {
            if (auto back_img = deck_.getCardBackImage()) {
                GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
                gdk_pixbuf_loader_write(loader, back_img->data.data(), back_img->data.size(), nullptr);
                gdk_pixbuf_loader_close(loader, nullptr);

                GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
                GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, CARD_WIDTH, CARD_HEIGHT, GDK_INTERP_BILINEAR);

                cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, CARD_WIDTH, CARD_HEIGHT);
                cairo_t* surface_cr = cairo_create(surface);
                gdk_cairo_set_source_pixbuf(surface_cr, scaled, 0, 0);
                cairo_paint(surface_cr);
                cairo_destroy(surface_cr);

                card_surface_cache_["back"] = surface;
                back_it = card_surface_cache_.find("back");

                g_object_unref(scaled);
                g_object_unref(loader);
            }
        }

        if (back_it != card_surface_cache_.end()) {
            cairo_set_source_surface(cr, back_it->second, x, y);
            cairo_paint(cr);
        }
    }
}

void ThirtyOneGame::drawStatusArea(cairo_t* cr, int width, int height) {
    int status_x = CARD_SPACING;
    int status_y = height - 100;
    
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    
    for (size_t i = 0; i < players_.size(); i++) {
        drawPlayerInfo(cr, players_[i], status_x, status_y, i == current_player_);
        status_x += 200;
    }
    
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_move_to(cr, CARD_SPACING, status_y - 30);
    
    std::string state_text = someone_knocked_ ? 
        "Last round after knock" : 
        (players_[current_player_].type == PlayerType::HUMAN ? 
            "Your turn" : "AI thinking...");
    
    cairo_show_text(cr, state_text.c_str());
}

void ThirtyOneGame::drawPlayerInfo(cairo_t* cr, const Player& player, int x, int y, bool is_current) {
    if (is_current) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    }
    
    cairo_move_to(cr, x, y);
    std::string player_text = player.name + 
        (player.type != PlayerType::HUMAN ? " (AI)" : "");
    cairo_show_text(cr, player_text.c_str());
    
    cairo_move_to(cr, x, y + 20);
    std::string token_text = "Tokens: " + std::to_string(player.tokens);
    if (player.on_bread_line) {
        token_text += " (Bread Line)";
    }
    cairo_show_text(cr, token_text.c_str());
    
    if (player.type != PlayerType::HUMAN || someone_knocked_) {
        cairo_move_to(cr, x, y + 40);
        std::string value_text = "Hand: " + 
            std::to_string(calculateHandValue(player.hand));
        cairo_show_text(cr, value_text.c_str());
    }
}

gboolean ThirtyOneGame::onAITimer(gpointer data) {
    ThirtyOneGame* game = static_cast<ThirtyOneGame*>(data);
    if (!game->game_paused_ && game->waiting_for_ai_) {
        game->performAITurn();
        game->ai_timer_id_ = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void ThirtyOneGame::setupAISpeedMenu(GtkWidget* gameMenu) {
    GtkWidget* aiSpeedMenu = gtk_menu_new();
    GtkWidget* aiSpeedItem = gtk_menu_item_new_with_label("AI Speed");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(aiSpeedItem), aiSpeedMenu);
    
    struct SpeedOption {
        const char* label;
        int ms;
    } speeds[] = {
        {"Fast (0.5s)", 500},
        {"Normal (1s)", 1000},
        {"Slow (2s)", 2000}
    };
    
    GSList* group = nullptr;
    for (const auto& speed : speeds) {
        GtkWidget* item = gtk_radio_menu_item_new_with_label(group, speed.label);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
        
        if (speed.ms == ai_speed_ms_) {
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
        }
        
        g_object_set_data(G_OBJECT(item), "speed-ms", GINT_TO_POINTER(speed.ms));
        g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(onAISpeedChanged), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(aiSpeedMenu), item);
    }
    
    gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), aiSpeedItem);
}

void ThirtyOneGame::onAISpeedChanged(GtkWidget* widget, gpointer data) {
    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
        return;
    }
    
    ThirtyOneGame* game = static_cast<ThirtyOneGame*>(data);
    game->ai_speed_ms_ = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(widget), "speed-ms"));
}

std::pair<int, int> ThirtyOneGame::getPileAt(int x, int y) const {
    if (x >= CARD_SPACING && x <= CARD_SPACING + CARD_WIDTH &&
        y >= CARD_SPACING && y <= CARD_SPACING + CARD_HEIGHT) {
        return {0, -1};
    }

    if (x >= 2 * CARD_SPACING + CARD_WIDTH &&
        x <= 2 * CARD_SPACING + 2 * CARD_WIDTH &&
        y >= CARD_SPACING && y <= CARD_SPACING + CARD_HEIGHT) {
        return {1, static_cast<int>(discard_pile_.size() - 1)};
    }

    int hand_y = CARD_SPACING * 2 + CARD_HEIGHT;
    if (y >= hand_y && y <= hand_y + CARD_HEIGHT) {
        int card_x = CARD_SPACING;
        for (size_t i = 0; i < players_[current_player_].hand.size(); ++i) {
            if (x >= card_x && x <= card_x + CARD_WIDTH) {
                return {2, static_cast<int>(i)};
            }
            card_x += CARD_WIDTH + CARD_SPACING;
        }
    }

    return {-1, -1};
}

bool ThirtyOneGame::isValidDragSource(int pile_index, int card_index) const {
    if (pile_index == 1) {
        return !discard_pile_.empty() && 
               card_index == static_cast<int>(discard_pile_.size() - 1);
    }
    else if (pile_index == 2) {
        return card_index >= 0 && 
               static_cast<size_t>(card_index) < players_[current_player_].hand.size();
    }
    return false;
}

gboolean ThirtyOneGame::onButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    ThirtyOneGame* game = static_cast<ThirtyOneGame*>(data);

    // Check if it's an AI's turn - if so, ignore player input
    if (game->players_[game->current_player_].type != PlayerType::HUMAN) {
        return TRUE;
    }

    if (event->button == 1) {
        auto [pile_index, card_index] = game->getPileAt(event->x, event->y);

        if (pile_index == 0) {
            game->drawFromDeck();
            gtk_widget_queue_draw(game->game_area_);
            return TRUE;
        }

        if (game->isValidDragSource(pile_index, card_index)) {
            game->dragging_ = true;
            game->drag_source_pile_ = pile_index;
            game->drag_card_index_ = card_index;
            game->drag_start_x_ = event->x;
            game->drag_start_y_ = event->y;

            int card_x;
            if (pile_index == 1) {
                card_x = 2 * game->CARD_SPACING + game->CARD_WIDTH;
                game->dragged_card_ = &game->discard_pile_.back();
            }
            else {
                card_x = game->CARD_SPACING + 
                    card_index * (game->CARD_WIDTH + game->CARD_SPACING);
                game->dragged_card_ = &game->players_[game->current_player_].hand[card_index];
            }

            game->drag_offset_x_ = event->x - card_x;
            game->drag_offset_y_ = event->y - 
                (pile_index == 1 ? game->CARD_SPACING : 
                 game->CARD_SPACING * 2 + game->CARD_HEIGHT);
        }
    }
    else if (event->button == 3) {
        auto& current_player = game->players_[game->current_player_];
        if (current_player.type == PlayerType::HUMAN && 
            game->calculateHandValue(current_player.hand) >= 21) {
            game->knock();
            gtk_widget_queue_draw(game->game_area_);
        }
    }

    return TRUE;
}

gboolean ThirtyOneGame::onButtonRelease(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    ThirtyOneGame* game = static_cast<ThirtyOneGame*>(data);

    // Check if it's an AI's turn - if so, ignore player input
    if (game->players_[game->current_player_].type != PlayerType::HUMAN) {
        return TRUE;
    }

    if (event->button == 1 && game->dragging_) {
        auto [target_pile, card_index] = game->getPileAt(event->x, event->y);

        if (target_pile == 1 && game->drag_source_pile_ == 2) {
            game->discard(game->drag_card_index_);
        }
        else if (target_pile == 2 && game->drag_source_pile_ == 1) {
            game->drawFromDiscard();
        }

        game->dragging_ = false;
        game->drag_source_pile_ = -1;
        game->drag_card_index_ = -1;
        game->dragged_card_ = nullptr;
        gtk_widget_queue_draw(game->game_area_);
    }

    return TRUE;
}

gboolean ThirtyOneGame::onMotionNotify(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    ThirtyOneGame* game = static_cast<ThirtyOneGame*>(data);

    if (game->players_[game->current_player_].type != PlayerType::HUMAN) {
        return TRUE;
    }

    if (game->dragging_) {
        game->drag_start_x_ = event->x;
        game->drag_start_y_ = event->y;
        gtk_widget_queue_draw(game->game_area_);
    }

    return TRUE;
}

void ThirtyOneGame::refreshDisplay() {
    if (game_area_) {
        gtk_widget_queue_draw(game_area_);
    }
}

void ThirtyOneGame::cleanupCardCache() {
    for (auto& [key, surface] : card_surface_cache_) {
        if (surface) {
            cairo_surface_destroy(surface);
        }
    }
    card_surface_cache_.clear();
}

void ThirtyOneGame::setupMenuBar() {
    GtkWidget* menubar = gtk_menu_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox_), menubar, FALSE, FALSE, 0);

    GtkWidget* gameMenu = gtk_menu_new();
    GtkWidget* gameMenuItem = gtk_menu_item_new_with_label("Game");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(gameMenuItem), gameMenu);

    GtkWidget* newGameItem = gtk_menu_item_new_with_label("New Game");
    g_signal_connect(G_OBJECT(newGameItem), "activate", G_CALLBACK(onNewGame), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), newGameItem);

    setupAISpeedMenu(gameMenu);

    GtkWidget* sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), sep);

    GtkWidget* quitItem = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(G_OBJECT(quitItem), "activate", G_CALLBACK(onQuit), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(gameMenu), quitItem);

    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), gameMenuItem);

    GtkWidget* helpMenu = gtk_menu_new();
    GtkWidget* helpMenuItem = gtk_menu_item_new_with_label("Help");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(helpMenuItem), helpMenu);

    GtkWidget* aboutItem = gtk_menu_item_new_with_label("About");
    g_signal_connect(G_OBJECT(aboutItem), "activate", G_CALLBACK(onAbout), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), aboutItem);

    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), helpMenuItem);
}

gboolean ThirtyOneGame::onDraw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    ThirtyOneGame* game = static_cast<ThirtyOneGame*>(data);

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    if (!game->buffer_surface_ ||
        cairo_image_surface_get_width(game->buffer_surface_) != allocation.width ||
        cairo_image_surface_get_height(game->buffer_surface_) != allocation.height) {

        if (game->buffer_surface_) {
            cairo_surface_destroy(game->buffer_surface_);
            cairo_destroy(game->buffer_cr_);
        }

        game->buffer_surface_ = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, allocation.width, allocation.height);
        game->buffer_cr_ = cairo_create(game->buffer_surface_);
    }

    cairo_set_source_rgb(game->buffer_cr_, 0.0, 0.5, 0.0);
    cairo_paint(game->buffer_cr_);

    int x = game->CARD_SPACING;
    int y = game->CARD_SPACING;

    game->drawCard(game->buffer_cr_, x, y, nullptr, false);

    x += game->CARD_WIDTH + game->CARD_SPACING;
    if (!game->discard_pile_.empty()) {
        const auto& top_card = game->discard_pile_.back();
        game->drawCard(game->buffer_cr_, x, y, &top_card, true);
    }

    x = game->CARD_SPACING;
    y = game->CARD_SPACING * 2 + game->CARD_HEIGHT;
    const auto& current_hand = game->players_[game->current_player_].hand;
    
    for (size_t i = 0; i < current_hand.size(); ++i) {
        if (!game->dragging_ || i != static_cast<size_t>(game->drag_card_index_)) {
            game->drawCard(game->buffer_cr_, x, y, &current_hand[i], 
                game->players_[game->current_player_].type == PlayerType::HUMAN);
        }
        x += game->CARD_WIDTH + game->CARD_SPACING;
    }

    if (game->dragging_ && game->dragged_card_) {
        game->drawCard(game->buffer_cr_, 
            game->drag_start_x_ - game->drag_offset_x_,
            game->drag_start_y_ - game->drag_offset_y_,
            game->dragged_card_, true);
    }

    game->drawStatusArea(game->buffer_cr_, allocation.width, allocation.height);

    cairo_set_source_surface(cr, game->buffer_surface_, 0, 0);
    cairo_paint(cr);

    return TRUE;
}

void ThirtyOneGame::onNewGame(GtkWidget* widget, gpointer data) {
    ThirtyOneGame* game = static_cast<ThirtyOneGame*>(data);
    game->startNewRound();
    gtk_widget_queue_draw(game->game_area_);
}

void ThirtyOneGame::onQuit(GtkWidget* widget, gpointer data) {
    gtk_main_quit();
}

void ThirtyOneGame::onAbout(GtkWidget* widget, gpointer data) {
    ThirtyOneGame* game = static_cast<ThirtyOneGame*>(data);

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "About Thirty One (Peaches/Knock)",
        GTK_WINDOW(game->window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "OK", GTK_RESPONSE_OK,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);

    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 15);

    GtkWidget* label = gtk_label_new(
        "Thirty One (also known as Peaches or Knock)\n\n"
        "A card game where players try to get 31 points in a single suit.\n"
        "Aces are worth 11, face cards 10, and number cards their face value.\n\n"
        "Special combinations:\n"
        "- 31 points in one suit: Everyone else loses a token\n"
        "- Three aces (any suit): Everyone else loses two tokens\n"
        "- Two face cards and an ace: Everyone else loses a token\n\n"
        "Players can knock with 21 or more points.\n"
        "Each player starts with 4 tokens.\n"
        "Last player with tokens wins!");

    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_container_add(GTK_CONTAINER(content_area), label);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

int main(int argc, char** argv) {
    ThirtyOneGame game;
    game.run(argc, argv);
    return 0;
}
