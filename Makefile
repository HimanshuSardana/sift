CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2
PKGS     := ncursesw sqlite3
CPPFLAGS += -Isrc $(shell pkg-config --cflags $(PKGS) 2>/dev/null)
LDLIBS   += $(shell pkg-config --libs $(PKGS) 2>/dev/null || echo -lncursesw -lsqlite3)

TARGET   := sift
SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:.cpp=.o)
LIB_OBJ  := $(filter-out src/main.o,$(OBJ))
TEST_BIN := tests/db_test

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(TEST_BIN): tests/db_test.cpp $(LIB_OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $^ $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_BIN)
