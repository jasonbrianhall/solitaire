# Compiler settings
CXX_LINUX = g++
CXX_WIN = x86_64-w64-mingw32-gcc
CXXFLAGS_COMMON = -std=c++17 -Wall -Wextra 

# Debug flags
DEBUG_FLAGS = -g -DDEBUG

# Platform-specific settings
CXXFLAGS_LINUX = $(CXXFLAGS_COMMON) $(shell pkg-config --cflags gtk+-3.0)
CXXFLAGS_WIN = $(CXXFLAGS_COMMON) $(shell mingw64-pkg-config --cflags gtk+-3.0)

# Debug-specific flags
CXXFLAGS_LINUX_DEBUG = $(CXXFLAGS_LINUX) $(DEBUG_FLAGS)
CXXFLAGS_WIN_DEBUG = $(CXXFLAGS_WIN) $(DEBUG_FLAGS)

LDFLAGS_LINUX = $(shell pkg-config --libs gtk+-3.0) -lzip
LDFLAGS_WIN = $(shell mingw64-pkg-config --libs gtk+-3.0) -lstdc++ -lzip -mwindows

# Source files and targets
SRCS = src/solitaire.cpp src/cardlib.cpp src/animation.cpp src/keyboard.cpp
OBJS_LINUX = $(SRCS:.cpp=.o)
OBJS_WIN = $(SRCS:.cpp=.win.o)
OBJS_LINUX_DEBUG = $(SRCS:.cpp=.debug.o)
OBJS_WIN_DEBUG = $(SRCS:.cpp=.win.debug.o)
TARGET_LINUX = solitaire
TARGET_WIN = solitaire.exe
TARGET_LINUX_DEBUG = solitaire_debug
TARGET_WIN_DEBUG = solitaire_debug.exe

# Build directories
BUILD_DIR = build
BUILD_DIR_LINUX = $(BUILD_DIR)/linux
BUILD_DIR_WIN = $(BUILD_DIR)/windows
BUILD_DIR_LINUX_DEBUG = $(BUILD_DIR)/linux_debug
BUILD_DIR_WIN_DEBUG = $(BUILD_DIR)/windows_debug

# Windows DLL settings
DLL_SOURCE_DIR = /usr/x86_64-w64-mingw32/sys-root/mingw/bin

# Create necessary directories
$(shell mkdir -p $(BUILD_DIR_LINUX)/src $(BUILD_DIR_WIN)/src $(BUILD_DIR_LINUX_DEBUG)/src $(BUILD_DIR_WIN_DEBUG)/src)

# Default target
.PHONY: all
all: linux

# Linux build targets
.PHONY: linux
linux: $(BUILD_DIR_LINUX)/$(TARGET_LINUX)

$(BUILD_DIR_LINUX)/$(TARGET_LINUX): $(addprefix $(BUILD_DIR_LINUX)/,$(OBJS_LINUX))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

$(BUILD_DIR_LINUX)/%.o: %.cpp
	$(CXX_LINUX) $(CXXFLAGS_LINUX) -c $< -o $@

# Linux debug targets
.PHONY: linux-debug
linux-debug: $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG)

$(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG): $(addprefix $(BUILD_DIR_LINUX_DEBUG)/,$(OBJS_LINUX_DEBUG))
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

$(BUILD_DIR_LINUX_DEBUG)/%.debug.o: %.cpp
	$(CXX_LINUX) $(CXXFLAGS_LINUX_DEBUG) -c $< -o $@

# Windows build targets
.PHONY: windows
windows: $(BUILD_DIR_WIN)/$(TARGET_WIN) collect-dlls

$(BUILD_DIR_WIN)/$(TARGET_WIN): $(addprefix $(BUILD_DIR_WIN)/,$(OBJS_WIN))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

$(BUILD_DIR_WIN)/%.win.o: %.cpp
	$(CXX_WIN) $(CXXFLAGS_WIN) -c $< -o $@

# Windows debug targets
.PHONY: windows-debug
windows-debug: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG) collect-debug-dlls

$(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG): $(addprefix $(BUILD_DIR_WIN_DEBUG)/,$(OBJS_WIN_DEBUG))
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

$(BUILD_DIR_WIN_DEBUG)/%.win.debug.o: %.cpp
	$(CXX_WIN) $(CXXFLAGS_WIN_DEBUG) -c $< -o $@

# DLL collection
.PHONY: collect-dlls
collect-dlls: $(BUILD_DIR_WIN)/$(TARGET_WIN)
	@echo "Collecting DLLs..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN)/$(TARGET_WIN) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN)

.PHONY: collect-debug-dlls
collect-debug-dlls: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG)
	@echo "Collecting Debug DLLs..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN_DEBUG)

# Clean targets
.PHONY: clean
clean:
	find build -type f -name "*.o" | xargs -I xxx rm xxx
	find build -type f -name "*.dll" | xargs -I xxx rm xxx
	find build -type f -name "*.exe" | xargs -I xxx rm xxx
	rm build/linux/solitaire -f
	rm build/linux_debug/solitaire_debug -f

# Help target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  make          - Build for Linux (default)"
	@echo "  make linux    - Build for Linux"
	@echo "  make linux-debug    - Build for Linux with debug symbols"
	@echo "  make windows  - Build for Windows (requires MinGW)"
	@echo "  make windows-debug  - Build for Windows with debug symbols"
	@echo "  make all      - Build for both Linux and Windows"
	@echo "  make clean    - Remove all build files"
	@echo "  make help     - Show this help message"
