#ifndef CARDLIB_H
#define CARDLIB_H

#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>
#include <numeric> // For std::accumulate
#include <algorithm>

namespace cardlib {

// Forward declaration of Deck class
class Deck;

enum class Suit { CLUBS, DIAMONDS, HEARTS, SPADES, JOKER };

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

  Card() : suit(Suit::CLUBS), rank(Rank::ACE), is_alternate_art(false) {}
  Card(Suit s, Rank r, bool alt = false)
      : suit(s), rank(r), is_alternate_art(alt) {}
  std::string toString() const;
};

struct CardImage {
  std::string filename;
  std::vector<unsigned char> data;
  std::optional<Card> card_info;
};

class MultiDeck {
public:
    // Constructors
    MultiDeck(size_t num_decks = 1);
    MultiDeck(size_t num_decks, const std::string &zip_path);

    // Deck operations that work across multiple decks
    void shuffle(unsigned seed = std::random_device{}());
    std::optional<Card> drawCard();
    void addCard(const Card &card);
    void addCardToBottom(const Card &card);
    bool isEmpty() const;
    size_t size() const;
    void reset(); // Resets all decks to initial state

    // Deck-specific operations
    void shuffleDeck(size_t deck_index, unsigned seed = std::random_device{}());
    std::optional<Card> drawCardFromDeck(size_t deck_index);
    std::vector<Card> getAllCardsInDeck(size_t deck_index) const;
    
    // Customization methods
    void includeJokersInAllDecks(bool include = true);
    void setAlternateArtInAllDecks(bool use_alternate = true);

    // Image operations
    std::optional<CardImage> getCardImage(const Card &card) const;
    std::optional<CardImage> getCardBackImage() const;

    // New methods for derived classes to access decks
    size_t getDeckCount() const { return decks_.size(); }
    Deck& getDeck(size_t index) { return decks_[index]; }
    const Deck& getDeck(size_t index) const { return decks_[index]; }

protected:
    // Change to protected to allow derived classes to modify decks
    std::vector<Deck> decks_;
    bool include_jokers_;
    bool use_alternate_art_;

    void initializeMultiDeck(size_t num_decks);
    void loadCardsFromZip(const std::string &zip_path, size_t num_decks);
};

class Deck {
public:
  // Constructors
  Deck();                                     // Creates a standard 52-card deck
  explicit Deck(const std::string &zip_path); // Loads cards from a ZIP file

  // Deck operations
  void shuffle(unsigned seed = std::random_device{}());
  std::optional<Card> drawCard();
  void addCard(const Card &card);
  void addCardToBottom(const Card &card);
  bool isEmpty() const;
  size_t size() const;
  void reset(); // Resets to initial state
  // Card access
  std::vector<Card> getAllCards() const;
  std::optional<Card> peekTopCard() const;
  std::optional<Card> peekBottomCard() const;
  std::optional<CardImage> getCardBackImage() const;

  void removeJokers();
  // Image operations
  std::optional<CardImage> getCardImage(const Card &card) const;

  // Deck customization
  void includeJokers(bool include = true);
  void setAlternateArt(bool use_alternate = true);
  void replaceCardBackImage(const std::string &image_path);

  // New method to filter cards
  void filterCards(const std::vector<Suit>& allowed_suits) {
    cards_.erase(
      std::remove_if(cards_.begin(), cards_.end(), 
        [&allowed_suits](const Card& card) {
          return std::find(allowed_suits.begin(), 
                           allowed_suits.end(), 
                           card.suit) == allowed_suits.end();
        }
      ),
      cards_.end()
    );
  }

private:
  std::vector<Card> cards_;
  std::vector<CardImage> card_images_;
  std::optional<CardImage> card_back_image_;
  bool include_jokers_;
  bool use_alternate_art_;

  void initializeStandardDeck();
  void loadCardsFromZip(const std::string &zip_path);
  static std::optional<Card> parseFilename(const std::string &filename);
};

// Utility functions
std::string suitToString(Suit suit);
std::string rankToString(Rank rank);
std::optional<Card> parseCardString(const std::string &card_str);

} // namespace cardlib

#endif // CARDLIB_H
