# 🃏 The Most Over-Engineered Card Games You'll Ever Love

Welcome to what might be the most meticulously crafted Solitaire implementation this side of the digital realm! Written in C++ with more love than your grandmother puts into her secret cookie recipe, this isn't just another card game – it's a labor of love with enough features to make Windows card games blush.

## ✨ Features

Currently includes three games
- ** Klondike ** aka class solitaire
- ** Freecell **
- ** Spider Solitaire **

### Common Features
- **Custom Card Backs**: For when the default just isn't fancy enough for your taste
- **Multiple Deck Support**: Load custom card decks via ZIP files (perfect for when you want to play with cats or dinosaurs instead of boring old Kings and Queens)
- **Fancy Graphics**: Powered by Cairo, because we believe pixels should be pretty
- **Super Smooth Drag & Drop**: More fluid than your morning smoothie
- **Full Keyboard Support**: Since sometimes you don't have a mouse
- **Auto-Complete Detection**: Because we know you were going to win anyway
- **Sound Effects**: Immersive audio feedback for every card movement

### Klondike (Classic Solitaire)
- **Draw-One or Draw-Three Mode**: Because sometimes you want it easy, and sometimes you want it Vegas-style

### Freecell
- **Four Free Cells**: Temporary storage spaces for strategic card movement
- **Smart Card Movement**: Automatic calculation of how many cards can be moved based on available free cells
- **Auto-Finish**: Let the computer complete the game when the path to victory is clear

### Spider Solitaire
- **Sequences**: Build up sequences to remove cards
- **Smart Card Movement**: Automatic calculation of how many cards can be moved based on suit
- **Auto-Finish**: Let the computer complete the game when the path to victory is clear
- **Difficulty**: Three levels of difficulty (Easy, Medium, and Hard)

## 🛠️ Building

### Prerequisites

- GTK+ 3.0 development libraries
- Cairo graphics library for pretty graphics
- libzip for opening archives
- A C++ compiler that doesn't faint at the sight of modern C++
- For Linux builds: PulseAudio development libraries for audio
- For Windows builds: MinGW-w64 with GTK+ development files (audio uses Windows native APIs)

### Build Steps

The project includes a versatile Makefile with numerous build targets:

```bash
make                  # Build all games for Linux (default)
make linux            # Build all games for Linux
make windows          # Build all games for Windows
make nameofgame       # Build (spider, freecell, solitaire) for Linux
make all-game         # Build game for both Windows and Linux
make all-linux        # Build all games for Linux
make all-windows      # Build alll games for Windows
make game-linux-debug # Build Game with debug Symbols and some debugging output
make all-debug        # Build all games with debug
make clean            # Clean up build files
make help             # Show available make targets
```

#### Linux Build
For a standard Linux build, simply run:
```bash
make
```

The executables will be created at `build/linux/solitaire` and `build/linux/freecell`

#### Windows Build
For Windows builds (requires MinGW-w64):
```bash
make windows
```

This will:
- Create the Windows executables at `build/windows/solitaire.exe` and `build/windows/freecell.exe`
- Automatically collect required DLLs
- Place everything in the `build/windows` directory

#### Individual Game Builds
If you want to build just one game:
```bash
make solitaire    # Build just Klondike Solitaire for Linux
make freecell     # Build just FreeCell for Linux
```

#### Debug Builds
For development with debug symbols:
```bash
make solitaire-linux-debug
make freecell-linux-debug
```

#### Build Features
- Uses C++17 standard
- Includes all necessary compiler warnings (-Wall -Wextra)
- Automatically handles platform-specific dependencies
- Separate build directories for Linux and Windows outputs
- Automated DLL collection for Windows builds

## 🎮 Playing the Games

### Common Controls

- **Left Click + Drag**: Move cards (revolutionary, we know)
- **Right Click**: Automatically move a card to its foundation (for the lazy among us)

### Keyboard Controls

#### Common
- **Arrow Keys**: Navigate around the board (yellow highlight)
- **Enter**: Select card (highlights to blue) or places in foundation
- **ESC**: Deselect card (turns highlight back to yellow)
- **F11**: Toggle fullscreen mode
- **CTRL+N**: New game
- **CTRL+Q**: Quit

#### Klondike (Solitaire)
- **Space**: Deal next card(s)
- **1**: Change to dealing one card
- **3**: Change to dealing three cards at once

#### Freecell
- **F**: Finish the game (auto-completes by moving cards to foundation if possible)
- **CTRL+R**: Restart game with the same seed

### Game Rules

#### Klondike (Classic Solitaire)
1. Build four foundation piles (♣,♦,♥,♠) from Ace to King
2. Stack cards in descending order with alternating colors in the tableau
3. Only Kings can fill empty tableau spots
4. Flip cards from the stock pile when you're stuck
5. Get all cards to the foundation piles to win

#### Freecell
1. Build four foundation piles (♣,♦,♥,♠) from Ace to King
2. Stack cards in descending order with alternating colors in the tableau
3. Use free cells for temporary card storage (one card each)
4. Only move as many cards at once as free cells and empty columns allow
5. Empty tableau spaces can be filled with any card
6. Get all cards to the foundation piles to win

#### SPIDER

Objective:
1. Create 8 complete sequences of cards from King down to Ace, all of the same suit.
2. When a sequence is complete, it is automatically removed from the table.

Game Setup:
1. - The game is played with 104 cards (2 decks)
2. - Cards are dealt into 10 tableau columns
3. - First 4 columns receive 6 cards each, last 6 columns receive 5 cards each
4. - Only the top card of each column is face up initially
5. - Remaining cards form the stock pile at the bottom of the screen

Rules:
1. You can move cards from one tableau column to another if they follow a descending sequence (regardless of suit)
2. To move a group of cards together, they must be in descending sequence AND of the same suit
3. Empty tableau spaces can be filled with any card or valid sequence
4. You can deal a new row of cards (one to each tableau column) by clicking the stock pile
5. You can only deal from the stock pile when all tableau columns have at least one card
6. When a sequence from King to Ace of the same suit is formed, it is automatically removed
7. The game is won when all 8 same-suit sequences have been completed

Difficulty Levels:
1. - Easy: 1 suit (all Spades)
2. - Medium: 2 suits (Spades and Hearts)
3. - Hard: 4 suits (standard deck)

## 🎨 Customization

### Custom Card Backs

1. Go to Game → Card Back → Select Custom Back
2. Choose your image file (PNG, JPG, JPEG supported)
3. Marvel at your sophisticated taste in card design

### Custom Card Decks

1. Create a ZIP file with your card images
2. Name them properly (e.g., "ace_of_spades.png", "king_of_hearts.png")
3. Include a "back.png" for the card back
4. Load your deck from Game → Load Deck
5. Enjoy your personally crafted cardtastic experience

## 🧪 Technical Bits

For the brave souls who dare to venture into the code:

- **Card Library**: A robust card management system that could probably handle a casino
- **Double Buffering**: Because we don't like screen tearing
- **Cairo Graphics**: Making rectangles look good since 2003
- **Event System**: More signals than a busy traffic intersection
- **Resource Management**: More careful with memory than your most frugal relative
- **Animation System**: Fluid card movements and celebratory fireworks when you win

## 🐛 Known Features (Not Bugs)

- The victory animation is intentionally chaotic with cards flying off the screen
- Cards occasionally show off their athletic abilities with smooth animations
- The auto-complete feature is actually quite smart (it's just shy)

## 🤝 Contributing

Found a way to make this over-engineered masterpiece even more over-engineered? We'd love to see it! Just:

1. Fork the repository
2. Create your feature branch
3. Commit your changes
4. Push to the branch
5. Open a Pull Request
6. Wait patiently while we admire your code

## 📜 License

MIT Licensed - because we believe in freedom (and not writing our own license text).

## 🙏 Acknowledgments

- The GTK+ team for making GUI development interesting
- Cairo developers for the pretty graphics
- The inventor of Solitaire and Freecell (whoever you are, you beautiful genius)
- Coffee ☕ - The true MVP of this project

## ✉️  Contact Author
Created by Jason Brian Hall ([jasonbrianhall@gmail.com](mailto:jasonbrianhall@gmail.com))

*Built with love, caffeine, and probably too many late-night coding sessions.*





## 🎮 Other Projects: The Coding Playground

Bored? Let me rescue you from the depths of monotony with these digital delights! 🚀

💣 **Minesweeper Madness**: [Minesweeper](https://github.com/jasonbrianhall/minesweeper) - Not just a game, it's a digital minefield of excitement! (It's actually a really good version, pinky promise! 🤞)

🧩 **Sudoku Solver Spectacular**: [Sudoku Solver](https://github.com/jasonbrianhall/sudoku_solver) - A Sudoku Swiss Army Knife! 🚀 This project is way more than just solving puzzles. Dive into a world where:
- 🧠 Puzzle Generation: Create brain-twisting Sudoku challenges
- 📄 MS-Word Magic: Generate professional puzzle documents
- 🚀 Extreme Solver: Crack instantaneously the most mind-bending Sudoku puzzles
- 🎮 Bonus Game Mode: Check out the playable version hidden in python_generated_puzzles

Numbers have never been this exciting! Prepare for a Sudoku adventure that'll make your brain cells do a happy dance! 🕺

🧊 **Rubik's Cube Chaos**: [Rubik's Cube Solver](https://github.com/jasonbrianhall/rubikscube/) - Crack the code of the most mind-bending 3x3 puzzle known to humanity! Solving optional, frustration guaranteed! 😅

🐛 **Willy the Worm's Wild Ride**: [Willy the worm](https://github.com/jasonbrianhall/willytheworm) - A 2D side-scroller starring the most adventurous invertebrate in gaming history! Who said worms can't be heroes? 🦸‍♂️

🧙‍♂️ **The Wizard's Castle: Choose Your Own Adventure**: [The Wizard's Castle](https://github.com/jasonbrianhall/wizardscastle) - A Text-Based RPG that works on QT5, CLI, and even Android! Magic knows no boundaries! ✨

🔤 **Hangman Hijinks**: [Hangman](https://github.com/jasonbrianhall/hangman) - Word-guessing mayhem in your terminal! Prepare for linguistic warfare! 💬

🕹️ **Bonus Level**: I've got a treasure trove of [more projects](https://github.com/jasonbrianhall) just waiting to be discovered! Some are shiny and new, some are old code that might need a digital retirement party. It's like a coding yard sale - you never know what gems you'll find! 🏴‍☠️

*Warning: Prolonged exposure may cause uncontrollable coding inspiration and spontaneous nerd moments* 🤓✨
