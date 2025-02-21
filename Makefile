CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17
PKG_CONFIG = `pkg-config --cflags --libs gtk+-3.0`
LIBS = -lzip

TARGET = solitaire
SRCS = src/solitaire.cpp
OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(PKG_CONFIG) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CONFIG) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
