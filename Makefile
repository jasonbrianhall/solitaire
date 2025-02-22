# Compiler settings
CXX_LINUX = g++
CXX_WIN = x86_64-w64-mingw32-gcc
CXXFLAGS_COMMON = -std=c++17 -Wall -Wextra 

# Platform-specific settings
CXXFLAGS_LINUX = $(CXXFLAGS_COMMON) $(shell pkg-config --cflags gtk+-3.0)
CXXFLAGS_WIN = $(CXXFLAGS_COMMON) $(shell mingw64-pkg-config --cflags gtk+-3.0)

LDFLAGS_LINUX = $(shell pkg-config --libs gtk+-3.0) -lzip
LDFLAGS_WIN = $(shell mingw64-pkg-config --libs gtk+-3.0) -lstdc++ -lzip -mwindows

# Source files and targets
SRCS = src/solitaire.cpp src/cardlib.cpp
OBJS_LINUX = $(SRCS:.cpp=.o)
OBJS_WIN = $(SRCS:.cpp=.win.o)
TARGET_LINUX = solitaire
TARGET_WIN = solitaire.exe

# Build directories
BUILD_DIR = build
BUILD_DIR_LINUX = $(BUILD_DIR)/linux
BUILD_DIR_WIN = $(BUILD_DIR)/windows

# Windows DLL settings
DLL_SOURCE_DIR = /usr/x86_64-w64-mingw32/sys-root/mingw/bin

# Default target
.PHONY: all
all: linux

# Linux build targets
.PHONY: linux
linux: $(BUILD_DIR_LINUX)/$(TARGET_LINUX)

$(BUILD_DIR_LINUX)/$(TARGET_LINUX): $(addprefix $(BUILD_DIR_LINUX)/,$(OBJS_LINUX))
	@mkdir -p $(BUILD_DIR_LINUX)
	$(CXX_LINUX) $^ -o $@ $(LDFLAGS_LINUX)

$(BUILD_DIR_LINUX)/%.o: %.cpp
	@mkdir -p $(BUILD_DIR_LINUX)
	$(CXX_LINUX) $(CXXFLAGS_LINUX) -c $< -o $@

# Windows build targets
.PHONY: windows
windows: $(BUILD_DIR_WIN)/$(TARGET_WIN) collect-dlls

$(BUILD_DIR_WIN)/$(TARGET_WIN): $(addprefix $(BUILD_DIR_WIN)/,$(OBJS_WIN))
	@mkdir -p $(BUILD_DIR_WIN)
	$(CXX_WIN) $^ -o $@ $(LDFLAGS_WIN)

$(BUILD_DIR_WIN)/%.win.o: %.cpp
	@mkdir -p $(BUILD_DIR_WIN)
	$(CXX_WIN) $(CXXFLAGS_WIN) -c $< -o $@

# DLL collection
.PHONY: collect-dlls
collect-dlls: $(BUILD_DIR_WIN)/$(TARGET_WIN)
	@echo "Collecting DLLs..."
	@build/windows/collect_dlls.sh $(BUILD_DIR_WIN)/$(TARGET_WIN) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN)

# Clean targets
.PHONY: clean
clean:
	find build -type f -name "*.o" | xargs -I xxx rm xxx
	find build -type f -name "*.dll" | xargs -I xxx rm xxx
	find build -type f -name "*.exe" | xargs -I xxx rm xxx
	rm build/linux/minesweeper -f

# Help target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  make          - Build for Linux (default)"
	@echo "  make linux    - Build for Linux"
	@echo "  make windows  - Build for Windows (requires MinGW)"
	@echo "  make all      - Build for both Linux and Windows"
	@echo "  make clean    - Remove all build files"
	@echo "  make help     - Show this help message"
