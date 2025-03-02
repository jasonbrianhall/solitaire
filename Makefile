# Combined Makefile for Solitaire and FreeCell
# Compiler settings
CXX_LINUX = g++
CXX_WIN = x86_64-w64-mingw32-gcc
CXXFLAGS_COMMON = -std=c++17 -Wall -Wextra 

# Debug flags
DEBUG_FLAGS = -g -DDEBUG

# Source files for Solitaire
SRCS_COMMON_SOLITAIRE = src_klondike/solitaire.cpp src_klondike/cardlib.cpp src_klondike/sound.cpp src_klondike/animation.cpp src_klondike/keyboard.cpp src_klondike/audiomanager.cpp
SRCS_LINUX_SOLITAIRE = src_klondike/pulseaudioplayer.cpp
SRCS_WIN_SOLITAIRE = src_klondike/windowsaudioplayer.cpp

# Source files for FreeCell
SRCS_COMMON_FREECELL = src_freecell/freecell.cpp src_freecell/cardlib.cpp src_freecell/keyboard.cpp src_freecell/mouse.cpp src_freecell/animation.cpp src_freecell/sound.cpp src_freecell/audiomanager.cpp
SRCS_LINUX_FREECELL = src_freecell/pulseaudioplayer.cpp 
SRCS_WIN_FREECELL = src_freecell/windowsaudioplayer.cpp

# Use pkg-config for dependencies
GTK_CFLAGS_LINUX := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS_LINUX := $(shell pkg-config --libs gtk+-3.0)
GTK_CFLAGS_WIN := $(shell mingw64-pkg-config --cflags gtk+-3.0)
GTK_LIBS_WIN := $(shell mingw64-pkg-config --libs gtk+-3.0)

# PulseAudio flags for Linux
PULSE_CFLAGS := $(shell pkg-config --cflags libpulse libpulse-simple)
PULSE_LIBS := $(shell pkg-config --libs libpulse libpulse-simple)

# ZIP library flags
ZIP_CFLAGS_LINUX := $(shell pkg-config --cflags libzip)
ZIP_LIBS_LINUX := $(shell pkg-config --libs libzip)
ZIP_CFLAGS_WIN := $(shell mingw64-pkg-config --cflags libzip)
ZIP_LIBS_WIN := $(shell mingw64-pkg-config --libs libzip)

# Platform-specific settings
CXXFLAGS_LINUX = $(CXXFLAGS_COMMON) $(GTK_CFLAGS_LINUX) $(PULSE_CFLAGS) $(ZIP_CFLAGS_LINUX)
CXXFLAGS_WIN = $(CXXFLAGS_COMMON) $(GTK_CFLAGS_WIN) $(ZIP_CFLAGS_WIN)

# Debug-specific flags
CXXFLAGS_LINUX_DEBUG = $(CXXFLAGS_LINUX) $(DEBUG_FLAGS)
CXXFLAGS_WIN_DEBUG = $(CXXFLAGS_WIN) $(DEBUG_FLAGS)

LDFLAGS_LINUX = $(GTK_LIBS_LINUX) $(PULSE_LIBS) $(ZIP_LIBS_LINUX) -pthread
LDFLAGS_WIN = $(GTK_LIBS_WIN) $(ZIP_LIBS_WIN) -lwinmm -lstdc++ -mwindows

# Object files for Solitaire
OBJS_LINUX_SOLITAIRE = $(SRCS_COMMON_SOLITAIRE:.cpp=.o) $(SRCS_LINUX_SOLITAIRE:.cpp=.o)
OBJS_WIN_SOLITAIRE = $(SRCS_COMMON_SOLITAIRE:.cpp=.win.o) $(SRCS_WIN_SOLITAIRE:.cpp=.win.o)
OBJS_LINUX_DEBUG_SOLITAIRE = $(SRCS_COMMON_SOLITAIRE:.cpp=.debug.o) $(SRCS_LINUX_SOLITAIRE:.cpp=.debug.o)
OBJS_WIN_DEBUG_SOLITAIRE = $(SRCS_COMMON_SOLITAIRE:.cpp=.win.debug.o) $(SRCS_WIN_SOLITAIRE:.cpp=.win.debug.o)

# Object files for FreeCell
OBJS_LINUX_FREECELL = $(SRCS_COMMON_FREECELL:.cpp=.o) $(SRCS_LINUX_FREECELL:.cpp=.o)
OBJS_WIN_FREECELL = $(SRCS_COMMON_FREECELL:.cpp=.win.o) $(SRCS_WIN_FREECELL:.cpp=.win.o)
OBJS_LINUX_DEBUG_FREECELL = $(SRCS_COMMON_FREECELL:.cpp=.debug.o) $(SRCS_LINUX_FREECELL:.cpp=.debug.o)
OBJS_WIN_DEBUG_FREECELL = $(SRCS_COMMON_FREECELL:.cpp=.win.debug.o) $(SRCS_WIN_FREECELL:.cpp=.win.debug.o)

# Target executables for Solitaire
TARGET_LINUX_SOLITAIRE = solitaire
TARGET_WIN_SOLITAIRE = solitaire.exe
TARGET_LINUX_DEBUG_SOLITAIRE = solitaire_debug
TARGET_WIN_DEBUG_SOLITAIRE = solitaire_debug.exe

# Target executables for FreeCell
TARGET_LINUX_FREECELL = freecell
TARGET_WIN_FREECELL = freecell.exe
TARGET_LINUX_DEBUG_FREECELL = freecell_debug
TARGET_WIN_DEBUG_FREECELL = freecell_debug.exe

# Build directories
BUILD_DIR = build
BUILD_DIR_LINUX = $(BUILD_DIR)/linux
BUILD_DIR_WIN = $(BUILD_DIR)/windows
BUILD_DIR_LINUX_DEBUG = $(BUILD_DIR)/linux_debug
BUILD_DIR_WIN_DEBUG = $(BUILD_DIR)/windows_debug

# Windows DLL settings
DLL_SOURCE_DIR = /usr/x86_64-w64-mingw32/sys-root/mingw/bin

# Create necessary directories
$(shell mkdir -p $(BUILD_DIR_LINUX)/src $(BUILD_DIR_LINUX)/src_freecell $(BUILD_DIR_WIN)/src $(BUILD_DIR_WIN)/src_freecell $(BUILD_DIR_LINUX_DEBUG)/src $(BUILD_DIR_LINUX_DEBUG)/src_freecell $(BUILD_DIR_WIN_DEBUG)/src $(BUILD_DIR_WIN_DEBUG)/src_freecell)

# Default target
.PHONY: all
all: solitaire-linux freecell-linux

windows: solitaire-windows freecell-windows

linux: solitaire-linux freecell-linux

# Combined targets
.PHONY: solitaire
solitaire: solitaire-linux

.PHONY: freecell
freecell: freecell-linux

.PHONY: all-solitaire
all-solitaire: solitaire-linux solitaire-windows

.PHONY: all-freecell
all-freecell: freecell-linux freecell-windows

.PHONY: all-linux
all-linux: solitaire-linux freecell-linux

.PHONY: all-windows
all-windows: solitaire-windows freecell-windows

.PHONY: all-debug
all-debug: solitaire-linux-debug solitaire-windows-debug freecell-linux-debug freecell-windows-debug

# Linux build targets for Solitaire
.PHONY: solitaire-linux
solitaire-linux: $(BUILD_DIR_LINUX)/$(TARGET_LINUX_SOLITAIRE)

$(BUILD_DIR_LINUX)/$(TARGET_LINUX_SOLITAIRE): $(addprefix $(BUILD_DIR_LINUX)/,$(OBJS_LINUX_SOLITAIRE))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Linux build targets for FreeCell
.PHONY: freecell-linux
freecell-linux: $(BUILD_DIR_LINUX)/$(TARGET_LINUX_FREECELL)

$(BUILD_DIR_LINUX)/$(TARGET_LINUX_FREECELL): $(addprefix $(BUILD_DIR_LINUX)/,$(OBJS_LINUX_FREECELL))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Generic compilation rules for Linux
$(BUILD_DIR_LINUX)/%.o: %.cpp
	$(CXX_LINUX) $(CXXFLAGS_LINUX) -c $< -o $@

# Linux debug targets for Solitaire
.PHONY: solitaire-linux-debug
solitaire-linux-debug: $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_SOLITAIRE)

$(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_SOLITAIRE): $(addprefix $(BUILD_DIR_LINUX_DEBUG)/,$(OBJS_LINUX_DEBUG_SOLITAIRE))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Linux debug targets for FreeCell
.PHONY: freecell-linux-debug
freecell-linux-debug: $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_FREECELL)

$(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_FREECELL): $(addprefix $(BUILD_DIR_LINUX_DEBUG)/,$(OBJS_LINUX_DEBUG_FREECELL))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Generic compilation rules for Linux debug
$(BUILD_DIR_LINUX_DEBUG)/%.debug.o: %.cpp
	$(CXX_LINUX) $(CXXFLAGS_LINUX_DEBUG) -c $< -o $@

# Windows build targets for Solitaire
.PHONY: solitaire-windows
solitaire-windows: $(BUILD_DIR_WIN)/$(TARGET_WIN_SOLITAIRE) solitaire-collect-dlls

$(BUILD_DIR_WIN)/$(TARGET_WIN_SOLITAIRE): $(addprefix $(BUILD_DIR_WIN)/,$(OBJS_WIN_SOLITAIRE))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Windows build targets for FreeCell
.PHONY: freecell-windows
freecell-windows: $(BUILD_DIR_WIN)/$(TARGET_WIN_FREECELL) freecell-collect-dlls

$(BUILD_DIR_WIN)/$(TARGET_WIN_FREECELL): $(addprefix $(BUILD_DIR_WIN)/,$(OBJS_WIN_FREECELL))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Generic compilation rules for Windows
$(BUILD_DIR_WIN)/%.win.o: %.cpp
	$(CXX_WIN) $(CXXFLAGS_WIN) -c $< -o $@

# Windows debug targets for Solitaire
.PHONY: solitaire-windows-debug
solitaire-windows-debug: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_SOLITAIRE) solitaire-collect-debug-dlls

$(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_SOLITAIRE): $(addprefix $(BUILD_DIR_WIN_DEBUG)/,$(OBJS_WIN_DEBUG_SOLITAIRE))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Windows debug targets for FreeCell
.PHONY: freecell-windows-debug
freecell-windows-debug: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_FREECELL) freecell-collect-debug-dlls

$(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_FREECELL): $(addprefix $(BUILD_DIR_WIN_DEBUG)/,$(OBJS_WIN_DEBUG_FREECELL))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Generic compilation rules for Windows debug
$(BUILD_DIR_WIN_DEBUG)/%.win.debug.o: %.cpp
	$(CXX_WIN) $(CXXFLAGS_WIN_DEBUG) -c $< -o $@

# DLL collection for Solitaire
.PHONY: solitaire-collect-dlls
solitaire-collect-dlls: $(BUILD_DIR_WIN)/$(TARGET_WIN_SOLITAIRE)
	@echo "Collecting DLLs for Solitaire..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN)/$(TARGET_WIN_SOLITAIRE) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN)

.PHONY: solitaire-collect-debug-dlls
solitaire-collect-debug-dlls: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_SOLITAIRE)
	@echo "Collecting Debug DLLs for Solitaire..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_SOLITAIRE) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN_DEBUG)

# DLL collection for FreeCell
.PHONY: freecell-collect-dlls
freecell-collect-dlls: $(BUILD_DIR_WIN)/$(TARGET_WIN_FREECELL)
	@echo "Collecting DLLs for FreeCell..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN)/$(TARGET_WIN_FREECELL) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN)

.PHONY: freecell-collect-debug-dlls
freecell-collect-debug-dlls: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_FREECELL)
	@echo "Collecting Debug DLLs for FreeCell..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_FREECELL) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN_DEBUG)

# Clean targets
.PHONY: clean
clean:
	find build -type f -name "*.o" | xargs -I xxx rm xxx
	find build -type f -name "*.dll" | xargs -I xxx rm xxx
	find build -type f -name "*.exe" | xargs -I xxx rm xxx
	rm -f $(BUILD_DIR_LINUX)/$(TARGET_LINUX_SOLITAIRE)
	rm -f $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_SOLITAIRE)
	rm -f $(BUILD_DIR_LINUX)/$(TARGET_LINUX_FREECELL)
	rm -f $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_FREECELL)

# Help target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  make                  - Build both games for Linux (default)"
	@echo "  make linux            - Build both games for Linux"
	@echo "  make windows          - Build both games for Windows"
	@echo "  make solitaire        - Build Solitaire for Linux"
	@echo "  make freecell         - Build FreeCell for Linux"
	@echo "  make all-solitaire    - Build Solitaire for Linux and Windows"
	@echo "  make all-freecell     - Build FreeCell for Linux and Windows"
	@echo "  make all-linux        - Build both games for Linux"
	@echo "  make all-windows      - Build both games for Windows"
	@echo "  make solitaire-linux  - Build Solitaire for Linux"
	@echo "  make freecell-linux   - Build FreeCell for Linux"
	@echo "  make solitaire-linux-debug - Build Solitaire for Linux with debug symbols"
	@echo "  make freecell-linux-debug  - Build FreeCell for Linux with debug symbols"
	@echo "  make solitaire-windows     - Build Solitaire for Windows (requires MinGW)"
	@echo "  make freecell-windows      - Build FreeCell for Windows (requires MinGW)"
	@echo "  make solitaire-windows-debug - Build Solitaire for Windows with debug symbols"
	@echo "  make freecell-windows-debug  - Build FreeCell for Windows with debug symbols"
	@echo "  make all-debug        - Build both games for Linux and Windows with debug symbols"
	@echo "  make clean            - Remove all build files"
	@echo "  make help             - Show this help message"
