# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++14 -Wall -Wextra -O2

# Directories
SRC_DIR = src
INC_DIR = include
LIB_DIR = lib

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)

# Object files
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=%.o)

# Executable name
TARGET = smartspeaker

# Include path
INCLUDES = -I$(INC_DIR)

# Libraries to link
LIBS = -llibc

# Library path
LDFLAGS = -L$(LIB_DIR)

# Default target
all: $(TARGET)

# Linking the program
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS) $(LIBS)

# Compiling source files
%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean up
clean:
	rm -f $(OBJECTS) $(TARGET)

# Phony targets
.PHONY: all clean
