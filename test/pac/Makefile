# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++14 -Wall -Wextra -O2

# Directories
SRC_DIR = .
INC_DIR = .
LIB_DIR = .

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)

# Object files
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# Executable name
TARGET = pac

BUILD_DIR = .

# Include paths
INCLUDES =

# Libraries to link
LIBS = -lpulse -lpulse-simple

# Library path
LDFLAGS =

# Default target
all: $(TARGET)

# Linking the program
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS) $(LIBS)

# Compiling source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean up
clean:
	rm -f $(OBJECTS) $(TARGET)

# Install on connected target
install: $(TARGET) config.ini
	adb push $(TARGET) /opt/ss/

# Phony targets
.PHONY: all clean
