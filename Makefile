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

# Include paths
LIB_INCS = -I/usr/include/glib-2.0 -I/usr/include/glib-2.0/include -I/usr/lib/arm-linux-gnueabihf/glib-2.0/include
INCLUDES = -I$(INC_DIR) -I$(LIB_INCS)

# Libraries to link
LIBS = -lglib-2.0

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

# Install on connected target
install: $(TARGET) config.ini
	adb push ./config.ini ./smartspeaker /opt/ss/

# Phony targets
.PHONY: all clean
