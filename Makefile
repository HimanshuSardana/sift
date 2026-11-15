CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2
LDLIBS   := $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncurses)
CPPFLAGS += $(shell pkg-config --cflags ncursesw 2>/dev/null)

TARGET := sift
SRC    := $(wildcard src/*.cpp)
OBJ    := $(SRC:.cpp=.o)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)
