# 🃏 The Most Over-Engineered Solitaire Game You'll Ever Love

Welcome to what might be the most meticulously crafted Solitaire implementation this side of the digital realm! Written in C++ with more love than your grandmother puts into her secret cookie recipe, this isn't just another card game – it's a labor of love with enough features to make Windows Solitaire blush.

## ✨ Features

- **Draw-One or Draw-Three Mode**: Because sometimes you want it easy, and sometimes you want it Vegas-style
- **Custom Card Backs**: For when the default just isn't fancy enough for your taste
- **Multiple Deck Support**: Load custom card decks via ZIP files (perfect for when you want to play with cats or dinosaurs instead of boring old Kings and Queens)
- **Fancy Graphics**: Powered by Cairo, because we believe pixels should be pretty
- **Super Smooth Drag & Drop**: More fluid than your morning smoothie
- **Auto-Complete Detection**: Because we know you were going to win anyway
- **Full Keyboard Support**:  Since sometimes you don't have a mouse.

## 🛠️ Building

### Prerequisites

- GTK+ 3.0 development libraries
- Cairo graphics library
- libzip
- A C++ compiler that doesn't faint at the sight of modern C++
- For Windows builds: MinGW-w64 with GTK+ development files

### Build Steps

The project includes a versatile Makefile with several build targets:

```bash
make        # Build for Linux (default)
make linux  # Explicitly build for Linux
make windows # Build for Windows (requires MinGW)
make all    # Build for both Linux and Windows
make clean  # Clean up build files
make help   # Show available make targets
```

#### Linux Build
For a standard Linux build, simply run:
```bash
make
```

The executable will be created at `build/linux/solitaire`

#### Windows Build
For Windows builds (requires MinGW-w64):
```bash
make windows
```

This will:
- Create the Windows executable at `build/windows/solitaire_gtk.exe`
- Automatically collect required DLLs
- Place everything in the `build/windows` directory

#### Build Features
- Uses C++17 standard
- Includes all necessary compiler warnings (-Wall -Wextra)
- Automatically handles platform-specific dependencies
- Separate build directories for Linux and Windows outputs
- Automated DLL collection for Windows builds

## 🎮 Playing the Game

### Basic Controls

- **Left Click + Drag**: Move cards (revolutionary, we know)
- **Right Click**: Automatically move a card to its foundation (for the lazy among us)
- **Left Click on Stock**: Draw new cards (draw one or three, depending on your mode)

### Keyboard Controls

- **Arrow Keys**: Navigate around the board (yellow highlight)
- **Enter**: Select card (highlights to blue) or puts in foundation
- **ESC**: Deselect card (turns highlight back to yellow)
- **F**: Finish the game
- **Space**: Deal next card
- **1**: Change to dealing one card
- **3**: Change to dealing three cards at once
- To move a card, select the card you would like to move, press enter, move with arrow keys to new location, and then press enter again

### Game Rules

For those who've been living under a rock or just really enjoy reading rules:

1. Build four foundation piles (♣,♦,♥,♠) from Ace to King
2. Stack cards in descending order with alternating colors in the tableau
3. Only Kings can fill empty tableau spots (they're quite territorial)
4. Flip cards from the stock pile when you're stuck
5. Get all cards to the foundation piles to win (and trigger our slightly over-enthusiastic victory animation)

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
- The inventor of Solitaire (whoever you are, you beautiful genius)
- Coffee ☕ - The true MVP of this project

## ✉️  Contact Author
Created by Jason Brian Hall ([jasonbrianhall@gmail.com](mailto:jasonbrianhall@gmail.com))

---

Remember: If you're not having fun, you're probably playing Minesweeper instead. 

*Built with love, caffeine, and probably too many late-night coding sessions.*
