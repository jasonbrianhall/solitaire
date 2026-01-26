# Combined Makefile for Spider, FreeCell, and Klondike Solitaire
# Compiler settings
CXX_LINUX = g++
CXX_WIN = x86_64-w64-mingw32-gcc
CXXFLAGS_COMMON = -std=c++17 -Wall -Wextra 

# Debug flags
DEBUG_FLAGS = -g -DDEBUG

# Source files for Klondike Solitaire
SRCS_COMMON_KLONDIKE = src_klondike/solitaire.cpp src_klondike/cardlib.cpp src_klondike/sound.cpp src_klondike/animation_cairo.cpp src_klondike/keyboard.cpp src_klondike/audiomanager.cpp src_klondike/mouse.cpp src_klondike/animation.cpp
SRCS_LINUX_KLONDIKE = src_klondike/pulseaudioplayer.cpp src_klondike/animation_gl.cpp 
SRCS_WIN_KLONDIKE = src_klondike/windowsaudioplayer.cpp

# Source files for Spider Solitaire
SRCS_COMMON_SPIDER = src_spider/spider.cpp src_spider/cardlib.cpp src_spider/sound.cpp src_spider/spider_animation.cpp src_spider/keyboard.cpp src_spider/audiomanager.cpp src_spider/spiderdeck.cpp
SRCS_LINUX_SPIDER = src_spider/pulseaudioplayer.cpp src_spider/spider_animation_gl.cpp
SRCS_WIN_SPIDER = src_spider/windowsaudioplayer.cpp

# Source files for FreeCell
SRCS_COMMON_FREECELL = src_freecell/freecell.cpp src_freecell/cardlib.cpp src_freecell/keyboard.cpp src_freecell/mouse.cpp src_freecell/animation.cpp src_freecell/sound.cpp src_freecell/audiomanager.cpp
SRCS_LINUX_FREECELL = src_freecell/pulseaudioplayer.cpp src_freecell/animation_gl.cpp
SRCS_WIN_FREECELL = src_freecell/windowsaudioplayer.cpp

# Source files for Pyramid Solitaire
SRCS_COMMON_PYRAMID = src_pyramid/solitaire.cpp src_pyramid/cardlib.cpp src_pyramid/sound.cpp src_pyramid/animation_cairo.cpp src_pyramid/keyboard.cpp src_pyramid/audiomanager.cpp src_pyramid/mouse.cpp src_pyramid/animation.cpp
SRCS_LINUX_PYRAMID = src_pyramid/pulseaudioplayer.cpp src_pyramid/animation_gl.cpp 
SRCS_WIN_PYRAMID = src_pyramid/windowsaudioplayer.cpp

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

# OpenGL flags for Linux (3.4+ with GLEW, GLFW3, GLM)
OPENGL_CFLAGS_LINUX := $(shell pkg-config --cflags gl glew glfw3)
OPENGL_LIBS_LINUX := $(shell pkg-config --libs gl glew glfw3)
# GLM is header-only, just add include path if needed
GLM_CFLAGS := -I/usr/include/glm

# Platform-specific settings
CXXFLAGS_LINUX = $(CXXFLAGS_COMMON) $(GTK_CFLAGS_LINUX) $(PULSE_CFLAGS) $(ZIP_CFLAGS_LINUX) $(OPENGL_CFLAGS_LINUX) $(GLM_CFLAGS)  -DUSEOPENGL
CXXFLAGS_WIN = $(CXXFLAGS_COMMON) $(GTK_CFLAGS_WIN) $(ZIP_CFLAGS_WIN)

# Debug-specific flags
CXXFLAGS_LINUX_DEBUG = $(CXXFLAGS_LINUX) $(DEBUG_FLAGS)
CXXFLAGS_WIN_DEBUG = $(CXXFLAGS_WIN) $(DEBUG_FLAGS)

LDFLAGS_LINUX = $(GTK_LIBS_LINUX) $(PULSE_LIBS) $(ZIP_LIBS_LINUX) $(OPENGL_LIBS_LINUX) -pthread
LDFLAGS_WIN = $(GTK_LIBS_WIN) $(ZIP_LIBS_WIN) -lwinmm -lstdc++ -mwindows

# Object files for Klondike Solitaire
OBJS_LINUX_KLONDIKE = $(SRCS_COMMON_KLONDIKE:.cpp=.o) $(SRCS_LINUX_KLONDIKE:.cpp=.o)
OBJS_WIN_KLONDIKE = $(SRCS_COMMON_KLONDIKE:.cpp=.win.o) $(SRCS_WIN_KLONDIKE:.cpp=.win.o)
OBJS_LINUX_DEBUG_KLONDIKE = $(SRCS_COMMON_KLONDIKE:.cpp=.debug.o) $(SRCS_LINUX_KLONDIKE:.cpp=.debug.o)
OBJS_WIN_DEBUG_KLONDIKE = $(SRCS_COMMON_KLONDIKE:.cpp=.win.debug.o) $(SRCS_WIN_KLONDIKE:.cpp=.win.debug.o)

# Object files for Spider Solitaire
OBJS_LINUX_SPIDER = $(SRCS_COMMON_SPIDER:.cpp=.o) $(SRCS_LINUX_SPIDER:.cpp=.o)
OBJS_WIN_SPIDER = $(SRCS_COMMON_SPIDER:.cpp=.win.o) $(SRCS_WIN_SPIDER:.cpp=.win.o)
OBJS_LINUX_DEBUG_SPIDER = $(SRCS_COMMON_SPIDER:.cpp=.debug.o) $(SRCS_LINUX_SPIDER:.cpp=.debug.o)
OBJS_WIN_DEBUG_SPIDER = $(SRCS_COMMON_SPIDER:.cpp=.win.debug.o) $(SRCS_WIN_SPIDER:.cpp=.win.debug.o)

# Object files for FreeCell
OBJS_LINUX_FREECELL = $(SRCS_COMMON_FREECELL:.cpp=.o) $(SRCS_LINUX_FREECELL:.cpp=.o)
OBJS_WIN_FREECELL = $(SRCS_COMMON_FREECELL:.cpp=.win.o) $(SRCS_WIN_FREECELL:.cpp=.win.o)
OBJS_LINUX_DEBUG_FREECELL = $(SRCS_COMMON_FREECELL:.cpp=.debug.o) $(SRCS_LINUX_FREECELL:.cpp=.debug.o)
OBJS_WIN_DEBUG_FREECELL = $(SRCS_COMMON_FREECELL:.cpp=.win.debug.o) $(SRCS_WIN_FREECELL:.cpp=.win.debug.o)

# Object files for Pyramid Solitaire
OBJS_LINUX_PYRAMID = $(SRCS_COMMON_PYRAMID:.cpp=.o) $(SRCS_LINUX_PYRAMID:.cpp=.o)
OBJS_WIN_PYRAMID = $(SRCS_COMMON_PYRAMID:.cpp=.win.o) $(SRCS_WIN_PYRAMID:.cpp=.win.o)
OBJS_LINUX_DEBUG_PYRAMID = $(SRCS_COMMON_PYRAMID:.cpp=.debug.o) $(SRCS_LINUX_PYRAMID:.cpp=.debug.o)
OBJS_WIN_DEBUG_PYRAMID = $(SRCS_COMMON_PYRAMID:.cpp=.win.debug.o) $(SRCS_WIN_PYRAMID:.cpp=.win.debug.o)

# Target executables for Klondike Solitaire
TARGET_LINUX_KLONDIKE = solitaire
TARGET_WIN_KLONDIKE = solitaire.exe
TARGET_LINUX_DEBUG_KLONDIKE = solitaire_debug
TARGET_WIN_DEBUG_KLONDIKE = solitaire_debug.exe

# Target executables for Spider Solitaire
TARGET_LINUX_SPIDER = spider
TARGET_WIN_SPIDER = spider.exe
TARGET_LINUX_DEBUG_SPIDER = spider_debug
TARGET_WIN_DEBUG_SPIDER = spider_debug.exe

# Target executables for FreeCell
TARGET_LINUX_FREECELL = freecell
TARGET_WIN_FREECELL = freecell.exe
TARGET_LINUX_DEBUG_FREECELL = freecell_debug
TARGET_WIN_DEBUG_FREECELL = freecell_debug.exe

# Target executables for Pyramid Solitaire
TARGET_LINUX_PYRAMID = pyramid
TARGET_WIN_PYRAMID = pyramid.exe
TARGET_LINUX_DEBUG_PYRAMID = pyramid_debug
TARGET_WIN_DEBUG_PYRAMID = pyramid_debug.exe

# Build directories
BUILD_DIR = build
BUILD_DIR_LINUX = $(BUILD_DIR)/linux
BUILD_DIR_WIN = $(BUILD_DIR)/windows
BUILD_DIR_LINUX_DEBUG = $(BUILD_DIR)/linux_debug
BUILD_DIR_WIN_DEBUG = $(BUILD_DIR)/windows_debug

# Windows DLL settings
DLL_SOURCE_DIR = /usr/x86_64-w64-mingw32/sys-root/mingw/bin

# Create necessary directories
$(shell mkdir -p $(BUILD_DIR_LINUX)/src_klondike $(BUILD_DIR_LINUX)/src_spider $(BUILD_DIR_LINUX)/src_freecell $(BUILD_DIR_LINUX)/src_pyramid \
	$(BUILD_DIR_WIN)/src_klondike $(BUILD_DIR_WIN)/src_spider $(BUILD_DIR_WIN)/src_freecell $(BUILD_DIR_WIN)/src_pyramid \
	$(BUILD_DIR_LINUX_DEBUG)/src_klondike $(BUILD_DIR_LINUX_DEBUG)/src_spider $(BUILD_DIR_LINUX_DEBUG)/src_freecell $(BUILD_DIR_LINUX_DEBUG)/src_pyramid \
	$(BUILD_DIR_WIN_DEBUG)/src_klondike $(BUILD_DIR_WIN_DEBUG)/src_spider $(BUILD_DIR_WIN_DEBUG)/src_freecell $(BUILD_DIR_WIN_DEBUG)/src_pyramid)

# Default target - build all games for Linux
.PHONY: all
all: klondike-linux spider-linux freecell-linux pyramid-linux

# OS-specific builds
.PHONY: windows
windows: klondike-windows spider-windows freecell-windows pyramid-windows

.PHONY: linux
linux: klondike-linux spider-linux freecell-linux pyramid-linux

# Individual game targets
.PHONY: klondike
klondike: klondike-linux

.PHONY: spider
spider: spider-linux

.PHONY: freecell
freecell: freecell-linux

.PHONY: pyramid
pyramid: pyramid-linux

.PHONY: solitaire
solitaire: klondike-linux

# Combined targets by game
.PHONY: all-klondike
all-klondike: klondike-linux klondike-windows

.PHONY: all-spider
all-spider: spider-linux spider-windows

.PHONY: all-freecell
all-freecell: freecell-linux freecell-windows

.PHONY: all-pyramid
all-pyramid: pyramid-linux pyramid-windows

.PHONY: all-solitaire
all-solitaire: klondike-linux klondike-windows

# Combined targets by platform
.PHONY: all-linux
all-linux: klondike-linux spider-linux freecell-linux pyramid-linux

.PHONY: all-windows
all-windows: klondike-windows spider-windows freecell-windows pyramid-windows

# Debug targets
.PHONY: all-debug
all-debug: klondike-linux-debug klondike-windows-debug spider-linux-debug spider-windows-debug freecell-linux-debug freecell-windows-debug pyramid-linux-debug pyramid-windows-debug

#
# Linux build targets
#

# Klondike Solitaire
.PHONY: klondike-linux
klondike-linux: $(BUILD_DIR_LINUX)/$(TARGET_LINUX_KLONDIKE)

$(BUILD_DIR_LINUX)/$(TARGET_LINUX_KLONDIKE): $(addprefix $(BUILD_DIR_LINUX)/,$(OBJS_LINUX_KLONDIKE))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Spider Solitaire
.PHONY: spider-linux
spider-linux: $(BUILD_DIR_LINUX)/$(TARGET_LINUX_SPIDER)

$(BUILD_DIR_LINUX)/$(TARGET_LINUX_SPIDER): $(addprefix $(BUILD_DIR_LINUX)/,$(OBJS_LINUX_SPIDER))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# FreeCell
.PHONY: freecell-linux
freecell-linux: $(BUILD_DIR_LINUX)/$(TARGET_LINUX_FREECELL)

$(BUILD_DIR_LINUX)/$(TARGET_LINUX_FREECELL): $(addprefix $(BUILD_DIR_LINUX)/,$(OBJS_LINUX_FREECELL))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Pyramid Solitaire
.PHONY: pyramid-linux
pyramid-linux: $(BUILD_DIR_LINUX)/$(TARGET_LINUX_PYRAMID)

$(BUILD_DIR_LINUX)/$(TARGET_LINUX_PYRAMID): $(addprefix $(BUILD_DIR_LINUX)/,$(OBJS_LINUX_PYRAMID))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Generic compilation rules for Linux
$(BUILD_DIR_LINUX)/%.o: %.cpp
	$(CXX_LINUX) $(CXXFLAGS_LINUX) -c $< -o $@

#
# Linux debug targets
#

# Klondike Solitaire
.PHONY: klondike-linux-debug
klondike-linux-debug: $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_KLONDIKE)

$(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_KLONDIKE): $(addprefix $(BUILD_DIR_LINUX_DEBUG)/,$(OBJS_LINUX_DEBUG_KLONDIKE))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Spider Solitaire
.PHONY: spider-linux-debug
spider-linux-debug: $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_SPIDER)

$(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_SPIDER): $(addprefix $(BUILD_DIR_LINUX_DEBUG)/,$(OBJS_LINUX_DEBUG_SPIDER))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# FreeCell
.PHONY: freecell-linux-debug
freecell-linux-debug: $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_FREECELL)

$(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_FREECELL): $(addprefix $(BUILD_DIR_LINUX_DEBUG)/,$(OBJS_LINUX_DEBUG_FREECELL))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Pyramid Solitaire
.PHONY: pyramid-linux-debug
pyramid-linux-debug: $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_PYRAMID)

$(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_PYRAMID): $(addprefix $(BUILD_DIR_LINUX_DEBUG)/,$(OBJS_LINUX_DEBUG_PYRAMID))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

# Generic compilation rules for Linux debug
$(BUILD_DIR_LINUX_DEBUG)/%.debug.o: %.cpp
	$(CXX_LINUX) $(CXXFLAGS_LINUX_DEBUG) -c $< -o $@

#
# Windows build targets
#

# Klondike Solitaire
.PHONY: klondike-windows
klondike-windows: $(BUILD_DIR_WIN)/$(TARGET_WIN_KLONDIKE) klondike-collect-dlls

$(BUILD_DIR_WIN)/$(TARGET_WIN_KLONDIKE): $(addprefix $(BUILD_DIR_WIN)/,$(OBJS_WIN_KLONDIKE))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Spider Solitaire
.PHONY: spider-windows
spider-windows: $(BUILD_DIR_WIN)/$(TARGET_WIN_SPIDER) spider-collect-dlls

$(BUILD_DIR_WIN)/$(TARGET_WIN_SPIDER): $(addprefix $(BUILD_DIR_WIN)/,$(OBJS_WIN_SPIDER))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# FreeCell
.PHONY: freecell-windows
freecell-windows: $(BUILD_DIR_WIN)/$(TARGET_WIN_FREECELL) freecell-collect-dlls

$(BUILD_DIR_WIN)/$(TARGET_WIN_FREECELL): $(addprefix $(BUILD_DIR_WIN)/,$(OBJS_WIN_FREECELL))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Pyramid Solitaire
.PHONY: pyramid-windows
pyramid-windows: $(BUILD_DIR_WIN)/$(TARGET_WIN_PYRAMID) pyramid-collect-dlls

$(BUILD_DIR_WIN)/$(TARGET_WIN_PYRAMID): $(addprefix $(BUILD_DIR_WIN)/,$(OBJS_WIN_PYRAMID))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Generic compilation rules for Windows
$(BUILD_DIR_WIN)/%.win.o: %.cpp
	$(CXX_WIN) $(CXXFLAGS_WIN) -c $< -o $@

#
# Windows debug targets
#

# Klondike Solitaire
.PHONY: klondike-windows-debug
klondike-windows-debug: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_KLONDIKE) klondike-collect-debug-dlls

$(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_KLONDIKE): $(addprefix $(BUILD_DIR_WIN_DEBUG)/,$(OBJS_WIN_DEBUG_KLONDIKE))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Spider Solitaire
.PHONY: spider-windows-debug
spider-windows-debug: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_SPIDER) spider-collect-debug-dlls

$(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_SPIDER): $(addprefix $(BUILD_DIR_WIN_DEBUG)/,$(OBJS_WIN_DEBUG_SPIDER))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# FreeCell
.PHONY: freecell-windows-debug
freecell-windows-debug: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_FREECELL) freecell-collect-debug-dlls

$(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_FREECELL): $(addprefix $(BUILD_DIR_WIN_DEBUG)/,$(OBJS_WIN_DEBUG_FREECELL))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Pyramid Solitaire
.PHONY: pyramid-windows-debug
pyramid-windows-debug: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_PYRAMID) pyramid-collect-debug-dlls

$(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_PYRAMID): $(addprefix $(BUILD_DIR_WIN_DEBUG)/,$(OBJS_WIN_DEBUG_PYRAMID))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

# Generic compilation rules for Windows debug
$(BUILD_DIR_WIN_DEBUG)/%.win.debug.o: %.cpp
	$(CXX_WIN) $(CXXFLAGS_WIN_DEBUG) -c $< -o $@

#
# DLL collection for Windows builds
#

# Klondike Solitaire
.PHONY: klondike-collect-dlls
klondike-collect-dlls: $(BUILD_DIR_WIN)/$(TARGET_WIN_KLONDIKE)
	@echo "Collecting DLLs for Klondike Solitaire..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN)/$(TARGET_WIN_KLONDIKE) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN)

.PHONY: klondike-collect-debug-dlls
klondike-collect-debug-dlls: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_KLONDIKE)
	@echo "Collecting Debug DLLs for Klondike Solitaire..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_KLONDIKE) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN_DEBUG)

# Spider Solitaire
.PHONY: spider-collect-dlls
spider-collect-dlls: $(BUILD_DIR_WIN)/$(TARGET_WIN_SPIDER)
	@echo "Collecting DLLs for Spider Solitaire..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN)/$(TARGET_WIN_SPIDER) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN)

.PHONY: spider-collect-debug-dlls
spider-collect-debug-dlls: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_SPIDER)
	@echo "Collecting Debug DLLs for Spider Solitaire..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_SPIDER) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN_DEBUG)

# FreeCell
.PHONY: freecell-collect-dlls
freecell-collect-dlls: $(BUILD_DIR_WIN)/$(TARGET_WIN_FREECELL)
	@echo "Collecting DLLs for FreeCell..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN)/$(TARGET_WIN_FREECELL) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN)

.PHONY: freecell-collect-debug-dlls
freecell-collect-debug-dlls: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_FREECELL)
	@echo "Collecting Debug DLLs for FreeCell..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_FREECELL) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN_DEBUG)

# Pyramid Solitaire
.PHONY: pyramid-collect-dlls
pyramid-collect-dlls: $(BUILD_DIR_WIN)/$(TARGET_WIN_PYRAMID)
	@echo "Collecting DLLs for Pyramid Solitaire..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN)/$(TARGET_WIN_PYRAMID) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN)

.PHONY: pyramid-collect-debug-dlls
pyramid-collect-debug-dlls: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_PYRAMID)
	@echo "Collecting Debug DLLs for Pyramid Solitaire..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG_PYRAMID) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN_DEBUG)

# Clean targets
.PHONY: clean
clean:
	find build -type f -name "*.o" | xargs -I xxx rm xxx
	find build -type f -name "*.dll" | xargs -I xxx rm xxx
	find build -type f -name "*.exe" | xargs -I xxx rm xxx
	rm -f $(BUILD_DIR_LINUX)/$(TARGET_LINUX_KLONDIKE)
	rm -f $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_KLONDIKE)
	rm -f $(BUILD_DIR_LINUX)/$(TARGET_LINUX_SPIDER)
	rm -f $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_SPIDER)
	rm -f $(BUILD_DIR_LINUX)/$(TARGET_LINUX_FREECELL)
	rm -f $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_FREECELL)
	rm -f $(BUILD_DIR_LINUX)/$(TARGET_LINUX_PYRAMID)
	rm -f $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG_PYRAMID)

# Help target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  make                  - Build all games for Linux (default) with OpenGL support"
	@echo "  make linux            - Build all games for Linux with OpenGL support"
	@echo "  make windows          - Build all games for Windows (Cairo only, no OpenGL)"
	@echo ""
	@echo "  make klondike         - Build Klondike Solitaire for Linux with dual-engine support"
	@echo "  make spider           - Build Spider Solitaire for Linux"
	@echo "  make freecell         - Build FreeCell for Linux"
	@echo "  make pyramid          - Build Pyramid Solitaire for Linux with dual-engine support"
	@echo "  make solitaire        - Alias for make klondike"
	@echo ""
	@echo "  make all-klondike     - Build Klondike Solitaire for Linux and Windows"
	@echo "  make all-spider       - Build Spider Solitaire for Linux and Windows"
	@echo "  make all-freecell     - Build FreeCell for Linux and Windows"
	@echo "  make all-pyramid      - Build Pyramid Solitaire for Linux and Windows"
	@echo "  make all-solitaire    - Alias for make all-klondike"
	@echo ""
	@echo "  make all-linux        - Build all games for Linux with OpenGL support"
	@echo "  make all-windows      - Build all games for Windows (no OpenGL)"
	@echo ""
	@echo "  make klondike-linux   - Build Klondike Solitaire for Linux with OpenGL support"
	@echo "  make spider-linux     - Build Spider Solitaire for Linux"
	@echo "  make freecell-linux   - Build FreeCell for Linux"
	@echo "  make pyramid-linux    - Build Pyramid Solitaire for Linux with OpenGL support"
	@echo ""
	@echo "  make klondike-linux-debug - Build Klondike Solitaire for Linux with debug symbols and OpenGL"
	@echo "  make spider-linux-debug   - Build Spider Solitaire for Linux with debug symbols"
	@echo "  make freecell-linux-debug - Build FreeCell for Linux with debug symbols"
	@echo "  make pyramid-linux-debug  - Build Pyramid Solitaire for Linux with debug symbols"
	@echo ""
	@echo "  make klondike-windows     - Build Klondike Solitaire for Windows (requires MinGW)"
	@echo "  make spider-windows       - Build Spider Solitaire for Windows (requires MinGW)"
	@echo "  make freecell-windows     - Build FreeCell for Windows (requires MinGW)"
	@echo "  make pyramid-windows      - Build Pyramid Solitaire for Windows (requires MinGW)"
	@echo ""
	@echo "  make klondike-windows-debug - Build Klondike Solitaire for Windows with debug symbols"
	@echo "  make spider-windows-debug   - Build Spider Solitaire for Windows with debug symbols"
	@echo "  make freecell-windows-debug - Build FreeCell for Windows with debug symbols"
	@echo "  make pyramid-windows-debug  - Build Pyramid Solitaire for Windows with debug symbols"
	@echo ""
	@echo "  make all-debug        - Build all games for Linux and Windows with debug symbols"
	@echo "  make clean            - Remove all build files"
	@echo "  make help             - Show this help message"
	@echo ""
	@echo "Klondike and Pyramid Solitaire now include dual-engine support:"
	@echo "  - Cairo (CPU-based, all platforms)"
	@echo "  - OpenGL 3.4 (GPU-accelerated, Linux only)"
	@echo ""
	@echo "To use OpenGL on Linux, ensure these packages are installed:"
	@echo "  Ubuntu: sudo apt-get install libgl1-mesa-dev libglew-dev libglfw3-dev libglm-dev"
	@echo "  Fedora: sudo dnf install mesa-libGL-devel glew-devel glfw-devel glm-devel"
