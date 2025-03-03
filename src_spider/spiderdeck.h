#ifndef SPIDERDECK_H
#define SPIDERDECK_H

#include "cardlib.h"
#include <iostream>
#include <map>
#include <set>

namespace cardlib {

class SpiderDeck : public MultiDeck {
public:
    // Constructor for Spider Solitaire
    // num_suits: 1 (all spades), 2 (spades and hearts), or 4 (all suits)
    explicit SpiderDeck(int num_suits = 2);
    
    // Utility method to print deck contents (optional, for debugging)
    void printDeckContents() const;
};

} // namespace cardlib

#endif // SPIDERDECK_H
