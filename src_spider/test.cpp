#include "cardlib.h"
#include "spiderdeck.h"
#include <iostream>

// Example usage
int main() {
    // Create a Spider Solitaire deck
    // 1 suit: 8 decks of spades (8 x 13 = 104 cards)
    // 2 suits: 4 decks (2 spades, 2 hearts)
    // 4 suits: 2 decks (half spades, half hearts)
    cardlib::SpiderDeck spider_deck(1);  // Only spades
    
    // Shuffle the entire multi-deck
    spider_deck.shuffle();
    
    // Print deck contents to verify
    spider_deck.printDeckContents();
    
    return 0;
}
