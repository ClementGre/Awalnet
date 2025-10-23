# Compiler settings
COMP = gcc
CFLAGS = -ansi -pedantic -Wall -g -std=c99

# Files
SRCS = $(shell find src -type f -name '*.c')
HEADS = $(shell find src -type f -name '*.h')
OBJ = $(addprefix bin/obj/, $(notdir $(SRCS:.c=.o)))

# Executable name
EXEC = bin/awalnet

# Default target
all: $(EXEC)

# Link object files to create executable
$(EXEC): $(OBJ)
	@mkdir -p bin
	$(COMP) $(OBJ) -o $(EXEC)

# Compile source files to object files
bin/obj/%.o: src/%.c $(HEADS)
	@mkdir -p bin/obj
	$(COMP) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf bin

# Phony targets
.PHONY: all clean
