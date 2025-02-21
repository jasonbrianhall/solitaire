#include "cardlib.h"
#include <zip.h>
#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace cardlib {

std::string Card::toString() const {
    if (rank == Rank::JOKER) {
        return suit == Suit::HEARTS ? "Red Joker" : "Black Joker";
    }
    
    std::string result = rankToString(rank) + " of " + suitToString(suit);
    if (is_alternate_art) {
        result += " (Alt)";
    }
    return result;
}

Deck::Deck() 
    : include_jokers_(false), use_alternate_art_(false) {
    initializeStandardDeck();
}

Deck::Deck(const std::string& zip_path)
    : include_jokers_(false), use_alternate_art_(false) {
    loadCardsFromZip(zip_path);
}

void Deck::shuffle(unsigned seed) {
    std::mt19937 gen(seed);
    std::shuffle(cards_.begin(), cards_.end(), gen);
}

std::optional<Card> Deck::drawCard() {
    if (cards_.empty()) {
        return std::nullopt;
    }
    
    Card card = cards_.back();
    cards_.pop_back();
    return card;
}

void Deck::addCard(const Card& card) {
    cards_.push_back(card);
}

void Deck::addCardToBottom(const Card& card) {
    cards_.insert(cards_.begin(), card);
}

bool Deck::isEmpty() const {
    return cards_.empty();
}

size_t Deck::size() const {
    return cards_.size();
}

void Deck::reset() {
    cards_.clear();
    initializeStandardDeck();
}

std::vector<Card> Deck::getAllCards() const {
    return cards_;
}

std::optional<Card> Deck::peekTopCard() const {
    if (cards_.empty()) {
        return std::nullopt;
    }
    return cards_.back();
}

std::optional<Card> Deck::peekBottomCard() const {
    if (cards_.empty()) {
        return std::nullopt;
    }
    return cards_.front();
}

std::optional<CardImage> Deck::getCardImage(const Card& card) const {
    auto it = std::find_if(card_images_.begin(), card_images_.end(),
        [&card](const CardImage& img) {
            return img.card_info && 
                   img.card_info->suit == card.suit && 
                   img.card_info->rank == card.rank &&
                   img.card_info->is_alternate_art == card.is_alternate_art;
        });
    
    if (it != card_images_.end()) {
        return *it;
    }
    return std::nullopt;
}

void Deck::includeJokers(bool include) {
    include_jokers_ = include;
    reset();
}

void Deck::setAlternateArt(bool use_alternate) {
    use_alternate_art_ = use_alternate;
    reset();
}

void Deck::initializeStandardDeck() {
    cards_.clear();
    
    std::vector<Suit> suits = {Suit::CLUBS, Suit::DIAMONDS, Suit::HEARTS, Suit::SPADES};
    std::vector<Rank> ranks = {
        Rank::ACE, Rank::TWO, Rank::THREE, Rank::FOUR, Rank::FIVE,
        Rank::SIX, Rank::SEVEN, Rank::EIGHT, Rank::NINE, Rank::TEN,
        Rank::JACK, Rank::QUEEN, Rank::KING
    };
    
    for (const auto& suit : suits) {
        for (const auto& rank : ranks) {
            cards_.emplace_back(suit, rank, use_alternate_art_);
        }
    }
    
    if (include_jokers_) {
        cards_.emplace_back(Suit::HEARTS, Rank::JOKER, use_alternate_art_);
        cards_.emplace_back(Suit::SPADES, Rank::JOKER, use_alternate_art_);
    }
}

void Deck::loadCardsFromZip(const std::string& zip_path) {
    int err;
    zip* archive = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    
    if (!archive) {
        throw std::runtime_error("Failed to open ZIP file: " + zip_path);
    }
    
    card_images_.clear();
    zip_int64_t num_entries = zip_get_num_entries(archive, 0);
    
    for (zip_int64_t i = 0; i < num_entries; i++) {
        const char* name = zip_get_name(archive, i, 0);
        if (!name || strstr(name, ".png") == nullptr) continue;
        
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
            CardImage card_img;
            card_img.filename = name;
            card_img.data = std::move(buffer);
            card_img.card_info = parseFilename(name);
            card_images_.push_back(std::move(card_img));
        }
    }
    
    zip_close(archive);
    
    // Initialize deck based on available card images
    cards_.clear();
    for (const auto& img : card_images_) {
        if (img.card_info) {
            cards_.push_back(*img.card_info);
        }
    }
}

std::optional<Card> Deck::parseFilename(const std::string& filename) {
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

    // Parse rank
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

    // Parse suit
    if (filename.find("_of_clubs") != std::string::npos) card.suit = Suit::CLUBS;
    else if (filename.find("_of_diamonds") != std::string::npos) card.suit = Suit::DIAMONDS;
    else if (filename.find("_of_hearts") != std::string::npos) card.suit = Suit::HEARTS;
    else if (filename.find("_of_spades") != std::string::npos) card.suit = Suit::SPADES;
    else return std::nullopt;

    return card;
}

// Utility functions implementation
std::string suitToString(Suit suit) {
    switch (suit) {
        case Suit::CLUBS: return "Clubs";
        case Suit::DIAMONDS: return "Diamonds";
        case Suit::HEARTS: return "Hearts";
        case Suit::SPADES: return "Spades";
        case Suit::JOKER: return "Joker";
        default: return "Unknown";
    }
}

std::string rankToString(Rank rank) {
    switch (rank) {
        case Rank::ACE: return "Ace";
        case Rank::TWO: return "2";
        case Rank::THREE: return "3";
        case Rank::FOUR: return "4";
        case Rank::FIVE: return "5";
        case Rank::SIX: return "6";
        case Rank::SEVEN: return "7";
        case Rank::EIGHT: return "8";
        case Rank::NINE: return "9";
        case Rank::TEN: return "10";
        case Rank::JACK: return "Jack";
        case Rank::QUEEN: return "Queen";
        case Rank::KING: return "King";
        case Rank::JOKER: return "Joker";
        default: return "Unknown";
    }
}

std::optional<Card> parseCardString(const std::string& card_str) {
    std::istringstream iss(card_str);
    std::string rank_str, of, suit_str;
    
    iss >> rank_str;
    if (rank_str == "Red" || rank_str == "Black") {
        iss >> suit_str;
        if (suit_str != "Joker") return std::nullopt;
        
        Card card;
        card.rank = Rank::JOKER;
        card.suit = (rank_str == "Red") ? Suit::HEARTS : Suit::SPADES;
        card.is_alternate_art = false;
        return card;
    }
    
    iss >> of >> suit_str;
    if (of != "of") return std::nullopt;
    
    Card card;
    card.is_alternate_art = false;
    
    // Parse rank
    if (rank_str == "Ace") card.rank = Rank::ACE;
    else if (rank_str == "2") card.rank = Rank::TWO;
    else if (rank_str == "3") card.rank = Rank::THREE;
    else if (rank_str == "4") card.rank = Rank::FOUR;
    else if (rank_str == "5") card.rank = Rank::FIVE;
    else if (rank_str == "6") card.rank = Rank::SIX;
    else if (rank_str
