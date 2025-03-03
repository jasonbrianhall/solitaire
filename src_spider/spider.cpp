// Spider Solitaire Deck Creation
#include "cardlib.h"
#include <iostream>
#include <map>
#include <set>

class SpiderDeck : public cardlib::MultiDeck {
public:
    // Constructor for Spider Solitaire
    // num_suits: 1 (all spades), 2 (spades and hearts), or 4 (all suits)
    SpiderDeck(int num_suits = 2) {
        // Clear existing decks
        decks_.clear();
        
        // Determine the number of decks to create
        int total_decks;
        switch(num_suits) {
            case 1: total_decks = 8; break;  // 8 decks of spades
            case 2: total_decks = 4; break;  // 4 decks (2 spades, 2 hearts)
            case 4: total_decks = 2; break;  // 2 decks (all suits)
            default:
                throw std::invalid_argument("Invalid number of suits");
        }
        
        // Suits to use based on num_suits
        std::vector<cardlib::Suit> suits_to_use;
        switch(num_suits) {
            case 1: 
                suits_to_use = {cardlib::Suit::SPADES}; 
                break;
            case 2: 
                suits_to_use = {cardlib::Suit::SPADES, cardlib::Suit::HEARTS}; 
                break;
            case 4: 
                suits_to_use = {
                    cardlib::Suit::SPADES, 
                    cardlib::Suit::HEARTS, 
                    cardlib::Suit::CLUBS, 
                    cardlib::Suit::DIAMONDS
                }; 
                break;
        }
        
        // Create decks
        for (int i = 0; i < total_decks; ++i) {
            // Create a full deck
            cardlib::Deck deck;
            
            // Filter to only allowed suits
            deck.filterCards(suits_to_use);
            
            // Add to decks
            decks_.push_back(deck);
        }
    }
    
    // Utility method to print deck contents
    void printDeckContents() const {
        for (size_t i = 0; i < this->decks_.size(); ++i) {
            std::cout << "Deck " << i << " contents:" << std::endl;
            
            // Count cards by rank
            std::map<cardlib::Rank, int> rank_counts;
            std::set<cardlib::Suit> deck_suits;
            for (const auto& card : this->decks_[i].getAllCards()) {
                std::cout << card.toString() << std::endl;
                rank_counts[card.rank]++;
                deck_suits.insert(card.suit);
            }
            
            std::cout << "Total cards: " << this->decks_[i].size() << std::endl;
            
            // Verify rank distribution
            std::cout << "Rank distribution:" << std::endl;
            for (const auto& pair : rank_counts) {
                std::cout << cardlib::rankToString(pair.first) << ": " << pair.second << std::endl;
            }
            
            // Verify suit distribution
            std::cout << "Suits in deck:" << std::endl;
            for (const auto& suit : deck_suits) {
                std::cout << cardlib::suitToString(suit) << std::endl;
            }
            std::cout << std::endl;
        }
    }
};

// Example usage
int main() {
    // Create a Spider Solitaire deck
    // 1 suit: 8 decks of spades (8 x 13 = 104 cards)
    // 2 suits: 4 decks (2 spades, 2 hearts)
    // 4 suits: 2 decks (half spades, half hearts)
    SpiderDeck spider_deck(1);  // Only spades
    
    // Shuffle the entire multi-deck
    spider_deck.shuffle();
    
    // Print deck contents to verify
    spider_deck.printDeckContents();
    
    return 0;
}
