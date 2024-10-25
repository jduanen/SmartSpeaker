# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++14 -Wall -Wextra -O2

# Directories
SRC_DIR = src
INC_DIR = include
LIB_DIR = lib

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/state/*.cpp)

# Object files
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# Executable name
TARGET = smartspeaker

BUILD_DIR = build

# Include paths
LIB_INCS = -I/usr/include/glib-2.0 -I/usr/include/glib-2.0/include -I/usr/lib/arm-linux-gnueabihf/glib-2.0/include
INCLUDES = -I$(INC_DIR) -I$(LIB_INCS) -I$(SRC_DIR)/state

# Libraries to link
LIBS = -lglib-2.0

# Library path
LDFLAGS = -L$(LIB_DIR)

# Default target
all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

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
	adb push ./config.ini ./smartspeaker /opt/ss/

# Phony targets
.PHONY: all clean
