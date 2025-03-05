#include "spiderdeck.h"
#include <stdexcept>

namespace cardlib {

SpiderDeck::SpiderDeck(int num_suits) {
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
    std::vector<Suit> suits_to_use;
    switch(num_suits) {
        case 1: 
            suits_to_use = {Suit::SPADES}; 
            break;
        case 2: 
            suits_to_use = {Suit::SPADES, Suit::HEARTS}; 
            break;
        case 4: 
            suits_to_use = {
                Suit::SPADES, 
                Suit::HEARTS, 
                Suit::CLUBS, 
                Suit::DIAMONDS
            }; 
            break;
    }
    
    // Create decks
    for (int i = 0; i < total_decks; ++i) {
        // Create a full deck
        Deck deck;
        
        // Filter to only allowed suits
        deck.filterCards(suits_to_use);
        
        // Add to decks
        decks_.push_back(deck);
    }
}

void SpiderDeck::printDeckContents() const {
    for (size_t i = 0; i < this->decks_.size(); ++i) {
        std::cout << "Deck " << i << " contents:" << std::endl;
        
        // Count cards by rank
        std::map<Rank, int> rank_counts;
        std::set<Suit> deck_suits;
        for (const auto& card : this->decks_[i].getAllCards()) {
            std::cout << card.toString() << std::endl;
            rank_counts[card.rank]++;
            deck_suits.insert(card.suit);
        }
        
        std::cout << "Total cards: " << this->decks_[i].size() << std::endl;
        
        // Verify rank distribution
        std::cout << "Rank distribution:" << std::endl;
        for (const auto& pair : rank_counts) {
            std::cout << rankToString(pair.first) << ": " << pair.second << std::endl;
        }
        
        // Verify suit distribution
        std::cout << "Suits in deck:" << std::endl;
        for (const auto& suit : deck_suits) {
            std::cout << suitToString(suit) << std::endl;
        }
        std::cout << std::endl;
    }
}

} // namespace cardlib
