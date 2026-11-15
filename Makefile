CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2
PKGS     := ncursesw sqlite3 libcurl libxml-2.0
DEPFLAGS := -MMD -MP
CPPFLAGS += -Isrc $(shell pkg-config --cflags $(PKGS) 2>/dev/null)
LDLIBS   += $(shell pkg-config --libs $(PKGS) 2>/dev/null || echo -lncursesw -lsqlite3 -lcurl -lxml2)

TARGET   := sift
SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:.cpp=.o)
LIB_OBJ  := $(filter-out src/main.o,$(OBJ))
TESTS    := tests/db_test tests/parse_test

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(CPPFLAGS) -c -o $@ $<

tests/%: tests/%.cpp $(LIB_OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $^ $(LDLIBS)

DEPS := $(OBJ:.o=.d)
-include $(DEPS)

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TESTS)
