#include <gtk/gtk.h>
#include <zip.h>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>

enum class Suit {
    CLUBS,
    DIAMONDS,
    HEARTS,
    SPADES,
    JOKER
};

enum class Rank {
    ACE = 1,
    TWO,
    THREE,
    FOUR,
    FIVE,
    SIX,
    SEVEN,
    EIGHT,
    NINE,
    TEN,
    JACK,
    QUEEN,
    KING,
    JOKER
};

struct Card {
    Suit suit;
    Rank rank;
    bool is_alternate_art;

    std::string toString() const {
        if (rank == Rank::JOKER) {
            return suit == Suit::HEARTS ? "Red Joker" : "Black Joker";
        }

        std::string rank_str;
        switch (rank) {
            case Rank::ACE: rank_str = "Ace"; break;
            case Rank::TWO: rank_str = "2"; break;
            case Rank::THREE: rank_str = "3"; break;
            case Rank::FOUR: rank_str = "4"; break;
            case Rank::FIVE: rank_str = "5"; break;
            case Rank::SIX: rank_str = "6"; break;
            case Rank::SEVEN: rank_str = "7"; break;
            case Rank::EIGHT: rank_str = "8"; break;
            case Rank::NINE: rank_str = "9"; break;
            case Rank::TEN: rank_str = "10"; break;
            case Rank::JACK: rank_str = "Jack"; break;
            case Rank::QUEEN: rank_str = "Queen"; break;
            case Rank::KING: rank_str = "King"; break;
            default: rank_str = "Unknown"; break;
        }

        std::string suit_str;
        switch (suit) {
            case Suit::CLUBS: suit_str = "Clubs"; break;
            case Suit::DIAMONDS: suit_str = "Diamonds"; break;
            case Suit::HEARTS: suit_str = "Hearts"; break;
            case Suit::SPADES: suit_str = "Spades"; break;
            default: suit_str = "Unknown"; break;
        }

        std::string result = rank_str + " of " + suit_str;
        if (is_alternate_art) {
            result += " (Alt)";
        }
        return result;
    }
};

struct CardImage {
    std::string filename;
    std::vector<unsigned char> data;
    std::optional<Card> card_info;
};

std::string find_cards_zip() {
    namespace fs = std::filesystem;
    fs::path current_path = fs::current_path();
    
    if (fs::exists(current_path / "cards.zip")) {
        return (current_path / "cards.zip").string();
    }
    
    if (fs::exists(current_path.parent_path() / "cards.zip")) {
        return (current_path.parent_path() / "cards.zip").string();
    }
    
    return "";
}

GtkWidget* create_error_dialog(const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(NULL,
                                             GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_CLOSE,
                                             "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), "Error");
    return dialog;
}

std::optional<Card> parse_filename(const std::string& filename) {
    Card card;
    card.is_alternate_art = false;

    if (filename == "red_joker.png") {
        card.rank = Rank::JOKER;
        card.suit = Suit::HEARTS;
        return card;
    }
    if (filename == "black_joker.png") {
        card.rank = Rank::JOKER;
        card.suit = Suit::SPADES;
        return card;
    }

    if (filename.length() > 5 && filename.substr(filename.length() - 5) == "2.png") {
        card.is_alternate_art = true;
    }

    if (filename.compare(0, 4, "ace_") == 0) card.rank = Rank::ACE;
    else if (filename.compare(0, 2, "2_") == 0) card.rank = Rank::TWO;
    else if (filename.compare(0, 2, "3_") == 0) card.rank = Rank::THREE;
    else if (filename.compare(0, 2, "4_") == 0) card.rank = Rank::FOUR;
    else if (filename.compare(0, 2, "5_") == 0) card.rank = Rank::FIVE;
    else if (filename.compare(0, 2, "6_") == 0) card.rank = Rank::SIX;
    else if (filename.compare(0, 2, "7_") == 0) card.rank = Rank::SEVEN;
    else if (filename.compare(0, 2, "8_") == 0) card.rank = Rank::EIGHT;
    else if (filename.compare(0, 2, "9_") == 0) card.rank = Rank::NINE;
    else if (filename.compare(0, 3, "10_") == 0) card.rank = Rank::TEN;
    else if (filename.compare(0, 5, "jack_") == 0) card.rank = Rank::JACK;
    else if (filename.compare(0, 6, "queen_") == 0) card.rank = Rank::QUEEN;
    else if (filename.compare(0, 5, "king_") == 0) card.rank = Rank::KING;
    else return std::nullopt;

    if (filename.find("_of_clubs") != std::string::npos) card.suit = Suit::CLUBS;
    else if (filename.find("_of_diamonds") != std::string::npos) card.suit = Suit::DIAMONDS;
    else if (filename.find("_of_hearts") != std::string::npos) card.suit = Suit::HEARTS;
    else if (filename.find("_of_spades") != std::string::npos) card.suit = Suit::SPADES;
    else return std::nullopt;

    return card;
}

std::vector<CardImage> load_cards_from_zip(const char* zipPath) {
    std::vector<CardImage> cards;
    int err;
    zip* archive = zip_open(zipPath, ZIP_RDONLY, &err);
    
    if (!archive) {
        zip_error_t ziperr;
        zip_error_init_with_code(&ziperr, err);
        std::cerr << "Error opening " << zipPath << ": " << zip_error_strerror(&ziperr) << std::endl;
        zip_error_fini(&ziperr);
        std::cerr << "Error opening " << zipPath << ": " << zip_error_strerror(&ziperr) << std::endl;
        return cards;
    }

    zip_int64_t num_entries = zip_get_num_entries(archive, 0);
    if (num_entries <= 0) {
        std::cerr << "No files found in " << zipPath << std::endl;
        zip_close(archive);
        return cards;
    }
    
    for (zip_int64_t i = 0; i < num_entries; i++) {
        const char* name = zip_get_name(archive, i, 0);
        if (!name) continue;
        
        if (strstr(name, ".png") != nullptr) {
            zip_file* file = zip_fopen(archive, name, 0);
            if (!file) continue;
            
            std::vector<unsigned char> buffer;
            const size_t chunk_size = 4096;
            unsigned char chunk[chunk_size];
            zip_int64_t bytesRead;
            
            while ((bytesRead = zip_fread(file, chunk, chunk_size)) > 0) {
                buffer.insert(buffer.end(), chunk, chunk + bytesRead);
            }
            
            zip_fclose(file);
            
            if (!buffer.empty()) {
                CardImage card;
                card.filename = name;
                card.data = std::move(buffer);
                card.card_info = parse_filename(name);
                cards.push_back(std::move(card));
            }
        }
    }
    
    zip_close(archive);
    
    std::sort(cards.begin(), cards.end(),
              [](const CardImage& a, const CardImage& b) {
                  if (!a.card_info || !b.card_info) {
                      return a.filename < b.filename;
                  }
                  
                  const Card& card_a = *a.card_info;
                  const Card& card_b = *b.card_info;
                  
                  if (card_a.rank != card_b.rank) {
                      return static_cast<int>(card_a.rank) < static_cast<int>(card_b.rank);
                  }
                  if (card_a.suit != card_b.suit) {
                      return static_cast<int>(card_a.suit) < static_cast<int>(card_b.suit);
                  }
                  return card_a.is_alternate_art < card_b.is_alternate_art;
              });
    
    return cards;
}

struct AppData {
    std::string zip_path;
    AppData(const std::string& path) : zip_path(path) {}
};

static void show_error_and_quit(GtkWindow* parent, const char* message) {
    GtkWidget* dialog = create_error_dialog(message);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    g_signal_connect_swapped(dialog, "response",
                           G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show_all(dialog);
}

static GtkWidget* create_card_widget(const CardImage& card) {
    GError* error = NULL;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    if (!loader) {
        return NULL;
    }

    gboolean write_success = FALSE;
    write_success = gdk_pixbuf_loader_write(loader, card.data.data(), card.data.size(), &error);
    
    if (!write_success) {
        if (error) {
            std::cerr << "Error loading image: " << error->message << std::endl;
            g_error_free(error);
        }
        g_object_unref(loader);
        return NULL;
    }

    if (!gdk_pixbuf_loader_close(loader, &error)) {
        if (error) {
            std::cerr << "Error closing loader: " << error->message << std::endl;
            g_error_free(error);
        }
        g_object_unref(loader);
        return NULL;
    }

    GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (!pixbuf) {
        g_object_unref(loader);
        return NULL;
    }

    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, 150, 218, GDK_INTERP_BILINEAR);
    g_object_unref(loader);

    if (!scaled) {
        return NULL;
    }

    GtkWidget* image = gtk_image_new_from_pixbuf(scaled);
    g_object_unref(scaled);

    if (!image) {
        return NULL;
    }

    std::string label = card.card_info ? card.card_info->toString() : card.filename;
    GtkWidget* frame = gtk_frame_new(label.c_str());
    if (!frame) {
        gtk_widget_destroy(image);
        return NULL;
    }

    gtk_container_add(GTK_CONTAINER(frame), image);
    return frame;
}

static void activate(GtkApplication* app, gpointer user_data) {
    AppData* app_data = static_cast<AppData*>(user_data);
    
    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), 
                        ("Card Deck Viewer - " + app_data->zip_path).c_str());
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);

    std::vector<CardImage> cards = load_cards_from_zip(app_data->zip_path.c_str());
    
    if (cards.empty()) {
        show_error_and_quit(GTK_WINDOW(window), 
                          "No card images found in the ZIP file.");
        return;
    }

    GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(window), scroll);
    
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(scroll), grid);

    const int COLS = 7;
    int row = 0, col = 0;
    
    for (const auto& card : cards) {
        GtkWidget* card_widget = create_card_widget(card);
        if (card_widget) {
            gtk_grid_attach(GTK_GRID(grid), card_widget, col, row, 1, 1);
            
            col++;
            if (col >= COLS) {
                col = 0;
                row++;
            }
        }
    }

    gtk_widget_show_all(window);
}

int main(int argc, char** argv) {
    std::string zip_path;
    
    if (argc > 1) {
        zip_path = argv[1];
    } else {
        zip_path = find_cards_zip();
        if (zip_path.empty()) {
            std::cerr << "Error: Could not find cards.zip in current or parent directory.\n"
                     << "Please provide the path to the ZIP file as an argument." << std::endl;
            return 1;
        }
    }

    AppData app_data(zip_path);
    
    GtkApplication* app = gtk_application_new("org.example.cardviewer",
                                            G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &app_data);
    
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return status;
}
