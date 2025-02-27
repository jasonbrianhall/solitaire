CXX_LINUX = g++
CXX_WIN = x86_64-w64-mingw32-gcc
CXXFLAGS_COMMON = -std=c++14 -Wall -Wextra

# Debug flags
DEBUG_FLAGS = -g -DDEBUG

# Source files
SRCS = audiomanager.cpp test.cpp
PLATFORM_SRCS_LINUX = pulseaudioplayer.cpp
PLATFORM_SRCS_WIN = windowsaudioplayer.cpp

# Use the pkg-config tool to get the right compile and link flags
ZIP_CFLAGS_LINUX := $(shell pkg-config --cflags libzip)
ZIP_LIBS_LINUX := $(shell pkg-config --libs libzip)
ZIP_CFLAGS_WIN := $(shell mingw64-pkg-config --cflags libzip)
ZIP_LIBS_WIN := $(shell mingw64-pkg-config --libs libzip)

# PulseAudio flags for Linux
PULSE_CFLAGS := $(shell pkg-config --cflags libpulse libpulse-simple)
PULSE_LIBS := $(shell pkg-config --libs libpulse libpulse-simple)

# Platform-specific settings
CXXFLAGS_LINUX = $(CXXFLAGS_COMMON) $(ZIP_CFLAGS_LINUX) $(PULSE_CFLAGS)
CXXFLAGS_WIN = $(CXXFLAGS_COMMON) $(ZIP_CFLAGS_WIN)

# Debug-specific flags
CXXFLAGS_LINUX_DEBUG = $(CXXFLAGS_LINUX) $(DEBUG_FLAGS)
CXXFLAGS_WIN_DEBUG = $(CXXFLAGS_WIN) $(DEBUG_FLAGS)

# Linker flags
LDFLAGS_LINUX = $(ZIP_LIBS_LINUX) $(PULSE_LIBS) -pthread
LDFLAGS_WIN = $(ZIP_LIBS_WIN) -lwinmm -lstdc++ -mwindows

# Object files
OBJS_LINUX = $(SRCS:.cpp=.o) $(PLATFORM_SRCS_LINUX:.cpp=.o)
OBJS_WIN = $(SRCS:.cpp=.win.o) $(PLATFORM_SRCS_WIN:.cpp=.win.o)
OBJS_LINUX_DEBUG = $(SRCS:.cpp=.debug.o) $(PLATFORM_SRCS_LINUX:.cpp=.debug.o)
OBJS_WIN_DEBUG = $(SRCS:.cpp=.win.debug.o) $(PLATFORM_SRCS_WIN:.cpp=.win.debug.o)

# Target executables
TARGET_LINUX = audiotest
TARGET_WIN = audiotest.exe
TARGET_LINUX_DEBUG = audiotest_debug
TARGET_WIN_DEBUG = audiotest_debug.exe

# Build directories
BUILD_DIR = build
BUILD_DIR_LINUX = $(BUILD_DIR)/linux
BUILD_DIR_WIN = $(BUILD_DIR)/windows
BUILD_DIR_LINUX_DEBUG = $(BUILD_DIR)/linux_debug
BUILD_DIR_WIN_DEBUG = $(BUILD_DIR)/windows_debug

# Windows DLL settings
DLL_SOURCE_DIR = /usr/x86_64-w64-mingw32/sys-root/mingw/bin

# Create necessary directories
$(shell mkdir -p $(BUILD_DIR_LINUX) $(BUILD_DIR_WIN) $(BUILD_DIR_LINUX_DEBUG) $(BUILD_DIR_WIN_DEBUG))

# Default target
.PHONY: all
all: linux

# Target for normal use (direct build)
$(TARGET_LINUX): $(OBJS_LINUX)
	$(CXX_LINUX) $(CXXFLAGS_LINUX) -o $@ $^ $(LDFLAGS_LINUX)

# Linux build targets
.PHONY: linux
linux: $(BUILD_DIR_LINUX)/$(TARGET_LINUX)

$(BUILD_DIR_LINUX)/$(TARGET_LINUX): $(addprefix $(BUILD_DIR_LINUX)/,$(notdir $(OBJS_LINUX)))
	$(CXX_LINUX) $(CXXFLAGS_LINUX) -o $@ $^ $(LDFLAGS_LINUX)

$(BUILD_DIR_LINUX)/%.o: %.cpp
	$(CXX_LINUX) $(CXXFLAGS_LINUX) -c $< -o $@

# Linux debug targets
.PHONY: linux-debug
linux-debug: $(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG)

$(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG): $(addprefix $(BUILD_DIR_LINUX_DEBUG)/,$(notdir $(OBJS_LINUX_DEBUG)))
	$(CXX_LINUX) $(CXXFLAGS_LINUX_DEBUG) -o $@ $^ $(LDFLAGS_LINUX)

$(BUILD_DIR_LINUX_DEBUG)/%.debug.o: %.cpp
	$(CXX_LINUX) $(CXXFLAGS_LINUX_DEBUG) -c $< -o $@

# Windows build targets
.PHONY: windows
windows: $(BUILD_DIR_WIN)/$(TARGET_WIN) collect-dlls

$(BUILD_DIR_WIN)/$(TARGET_WIN): $(addprefix $(BUILD_DIR_WIN)/,$(notdir $(OBJS_WIN)))
	$(CXX_WIN) $(CXXFLAGS_WIN) -o $@ $^ $(LDFLAGS_WIN)

$(BUILD_DIR_WIN)/%.win.o: %.cpp
	$(CXX_WIN) $(CXXFLAGS_WIN) -c $< -o $@

# Windows debug targets
.PHONY: windows-debug
windows-debug: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG) collect-debug-dlls

$(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG): $(addprefix $(BUILD_DIR_WIN_DEBUG)/,$(notdir $(OBJS_WIN_DEBUG)))
	$(CXX_WIN) $(CXXFLAGS_WIN_DEBUG) -o $@ $^ $(LDFLAGS_WIN)

$(BUILD_DIR_WIN_DEBUG)/%.win.debug.o: %.cpp
	$(CXX_WIN) $(CXXFLAGS_WIN_DEBUG) -c $< -o $@

# DLL collection using your existing script
.PHONY: collect-dlls
collect-dlls: $(BUILD_DIR_WIN)/$(TARGET_WIN)
	@echo "Collecting DLLs..."
	@$(BUILD_DIR)/windows/collect_dlls.sh $(BUILD_DIR_WIN)/$(TARGET_WIN) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN)

.PHONY: collect-debug-dlls
collect-debug-dlls: $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG)
	@echo "Collecting Debug DLLs..."
	@$(BUILD_DIR)/windows/collect_dlls.sh $(BUILD_DIR_WIN_DEBUG)/$(TARGET_WIN_DEBUG) $(DLL_SOURCE_DIR) $(BUILD_DIR_WIN_DEBUG)

# Check dependencies
.PHONY: check-deps
check-deps:
	@echo "Checking dependencies..."
	@if ! pkg-config --exists libzip; then \
		echo "Error: libzip not found. Install with:"; \
		echo "  Ubuntu/Debian: sudo apt-get install libzip-dev"; \
		echo "  Fedora: sudo dnf install libzip-devel"; \
		echo "  Arch Linux: sudo pacman -S libzip"; \
		exit 1; \
	fi
	@if ! pkg-config --exists libpulse libpulse-simple; then \
		echo "Error: PulseAudio libs not found. Install with:"; \
		echo "  Ubuntu/Debian: sudo apt-get install libpulse-dev"; \
		echo "  Fedora: sudo dnf install pulseaudio-libs-devel"; \
		echo "  Arch Linux: sudo pacman -S libpulse"; \
		exit 1; \
	fi
	@echo "All dependencies found."

# Clean targets
.PHONY: clean
clean:
	rm -f $(OBJS_LINUX) $(OBJS_WIN) $(OBJS_LINUX_DEBUG) $(OBJS_WIN_DEBUG)
	rm -f $(TARGET_LINUX) $(TARGET_WIN) $(TARGET_LINUX_DEBUG) $(TARGET_WIN_DEBUG)
	rm -rf $(BUILD_DIR_LINUX)/* $(BUILD_DIR_WIN)/* $(BUILD_DIR_LINUX_DEBUG)/* $(BUILD_DIR_WIN_DEBUG)/*
	mkdir -p $(BUILD_DIR_LINUX) $(BUILD_DIR_WIN) $(BUILD_DIR_LINUX_DEBUG) $(BUILD_DIR_WIN_DEBUG)

# Run the program with ZIP and WAV file arguments
.PHONY: run
run: linux
	./$(BUILD_DIR_LINUX)/$(TARGET_LINUX) $(ZIP_FILE) $(WAV_FILE_IN_ZIP)

.PHONY: run-debug
run-debug: linux-debug
	./$(BUILD_DIR_LINUX_DEBUG)/$(TARGET_LINUX_DEBUG) $(ZIP_FILE) $(WAV_FILE_IN_ZIP)

# Show help
.PHONY: help
help:
	@echo "Makefile for AudioManager with ZIP support and cross-compilation"
	@echo ""
	@echo "Targets:"
	@echo "  all              Build for Linux (default)"
	@echo "  linux            Build for Linux"
	@echo "  linux-debug      Build for Linux with debug symbols"
	@echo "  windows          Build for Windows (requires MinGW)"
	@echo "  windows-debug    Build for Windows with debug symbols"
	@echo "  check-deps       Check for required dependencies"
	@echo "  clean            Remove all build files"
	@echo "  run              Run the Linux build (requires ZIP_FILE and WAV_FILE_IN_ZIP variables)"
	@echo "  run-debug        Run the Linux debug build (requires ZIP_FILE and WAV_FILE_IN_ZIP variables)"
	@echo "  help             Show this help message"
	@echo ""
	@echo "Example usage:"
	@echo "  make"
	@echo "  make windows"
	@echo "  make run ZIP_FILE=sounds.zip WAV_FILE_IN_ZIP=music/background.wav"
	@echo "  make windows-debug"
