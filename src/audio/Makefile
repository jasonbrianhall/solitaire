CXX = g++
CXXFLAGS = -std=c++14 -Wall -Wextra
LDFLAGS = 

# Source files
SRCS = audiomanager.cpp test.cpp

# Detect the operating system
UNAME := $(shell uname)

# Use the pkg-config tool to get the right compile and link flags
ZIP_CFLAGS := $(shell pkg-config --cflags libzip)
ZIP_LIBS := $(shell pkg-config --libs libzip)

# Add ZIP flags to our compile and link flags
CXXFLAGS += $(ZIP_CFLAGS)
LDFLAGS += $(ZIP_LIBS)

# Platform-specific settings
ifeq ($(UNAME), MINGW32_NT-6.2)
    # Windows
    PLATFORM_SRCS = windowsaudioplayer.cpp
    LDFLAGS += -lwinmm
else ifeq ($(UNAME), MINGW64_NT-10.0)
    # Windows
    PLATFORM_SRCS = windowsaudioplayer.cpp
    LDFLAGS += -lwinmm
else ifeq ($(OS), Windows_NT)
    # Windows
    PLATFORM_SRCS = windowsaudioplayer.cpp
    LDFLAGS += -lwinmm
else
    # Linux/Unix
    PLATFORM_SRCS = pulseaudioplayer.cpp
    PULSE_CFLAGS := $(shell pkg-config --cflags libpulse libpulse-simple)
    PULSE_LIBS := $(shell pkg-config --libs libpulse libpulse-simple)
    CXXFLAGS += $(PULSE_CFLAGS)
    LDFLAGS += $(PULSE_LIBS) -pthread
endif

# All source files
ALL_SRCS = $(SRCS) $(PLATFORM_SRCS)

# Object files
OBJS = $(ALL_SRCS:.cpp=.o)

# Target executable
TARGET = audiotest

# Default target
all: $(TARGET)

# Linking rule
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Compile rule
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@if ! pkg-config --exists libzip; then \
		echo "Error: libzip not found. Install with:"; \
		echo "  Ubuntu/Debian: sudo apt-get install libzip-dev"; \
		echo "  Fedora: sudo dnf install libzip-devel"; \
		echo "  Arch Linux: sudo pacman -S libzip"; \
		exit 1; \
	fi
	@if [ "$(UNAME)" != "MINGW32_NT-6.2" ] && [ "$(UNAME)" != "MINGW64_NT-10.0" ] && [ "$(OS)" != "Windows_NT" ]; then \
		if ! pkg-config --exists libpulse libpulse-simple; then \
			echo "Error: PulseAudio libs not found. Install with:"; \
			echo "  Ubuntu/Debian: sudo apt-get install libpulse-dev"; \
			echo "  Fedora: sudo dnf install pulseaudio-libs-devel"; \
			echo "  Arch Linux: sudo pacman -S libpulse"; \
			exit 1; \
		fi; \
	fi
	@echo "All dependencies found."

# Clean up
clean:
	rm -f $(OBJS) $(TARGET)

# Run the program with ZIP and WAV file arguments
run: $(TARGET)
	./$(TARGET) $(ZIP_FILE) $(WAV_FILE_IN_ZIP)

# Show help
help:
	@echo "Makefile for AudioManager with ZIP support"
	@echo ""
	@echo "Targets:"
	@echo "  all         Build the program (default)"
	@echo "  check-deps  Check for required dependencies"
	@echo "  clean       Remove built files"
	@echo "  run         Run the program (requires ZIP_FILE and WAV_FILE_IN_ZIP variables)"
	@echo "  help        Show this help message"
	@echo ""
	@echo "Example usage:"
	@echo "  make"
	@echo "  make run ZIP_FILE=sounds.zip WAV_FILE_IN_ZIP=music/background.wav"

.PHONY: all check-deps clean run help
