CXX = clang++
CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -march=native -mtune=native

SRCDIR = src
OBJDIR = obj

SRCS = $(SRCDIR)/main.cpp
OBJS = $(OBJDIR)/main.o

TARGET = tictactoe_cpp

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all clean
