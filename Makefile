# Compiler settings
CC = gcc
CFLAGS = -ansi -pedantic -Wall -g -std=c99

# Séparer les sources
SRCS_COMMON = $(shell find src/common -type f -name '*.c')
SRCS_SERVER = $(shell find src/server -type f -name '*.c')
SRCS_CLIENT = $(shell find src/client -type f -name '*.c')
HEADS = $(shell find src -type f -name '*.h')

# Objets pour chaque cible (server/client partagent common)
OBJ_SERVER = $(patsubst src/%.c, bin/obj/%.o, $(SRCS_SERVER) $(SRCS_COMMON))
OBJ_CLIENT = $(patsubst src/%.c, bin/obj/%.o, $(SRCS_CLIENT) $(SRCS_COMMON))

# Exécutables
EXE_SERVER = bin/awalnet_server
EXE_CLIENT = bin/awalnet_client

# Default target: build both
all: build_all

# Build rules for server and client
build_server: $(EXE_SERVER)

$(EXE_SERVER): $(OBJ_SERVER)
	@mkdir -p $(dir $@)
	$(CC) $(OBJ_SERVER) -o $(EXE_SERVER)

build_client: $(EXE_CLIENT)

$(EXE_CLIENT): $(OBJ_CLIENT)
	@mkdir -p $(dir $@)
	$(CC) $(OBJ_CLIENT) -o $(EXE_CLIENT)

# Generic object compilation rule
bin/obj/%.o: src/%.c $(HEADS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Run targets (build first)
run_server: build_server
	@echo "Lancement du serveur..."
	$(EXE_SERVER)

run_client: build_client
	@echo "Lancement du client..."
	$(EXE_CLIENT)

# Build both in parallel
build_all:
	@echo "Démarrage des builds server et client en parallèle..."
	@$(MAKE) build_server & $(MAKE) build_client & wait
	@echo "Builds terminés."

# Run both in parallel (builds d'abord)
run_all: build_all
	@echo "Lancement server et client en parallèle..."
	@$(EXE_SERVER) & $(EXE_CLIENT) & wait

# Clean build artifacts
clean:
	rm -rf bin

# Phony targets
.PHONY: all build_server build_client build_all run_server run_client run_all clean
