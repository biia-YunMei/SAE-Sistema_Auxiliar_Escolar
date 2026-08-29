# =============================================================================
#  School System Backend - Makefile
#  Linguagem: C11 | Compilador: gcc (clang compatível)
# =============================================================================

CC          ?= cc
STD         := -std=c11
WARN        := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wstrict-prototypes \
               -Wmissing-prototypes -Wpointer-arith -Wcast-align -Wno-unused-parameter
OPT         := -O2
DBG         := -g3 -DDEBUG
SAN         := -fsanitize=address,undefined -fno-omit-frame-pointer

SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS   := $(shell pkg-config --libs sqlite3 2>/dev/null)
CURL_CFLAGS   := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS     := $(shell pkg-config --libs libcurl 2>/dev/null)
CRYPTO_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
CRYPTO_LIBS   := $(shell pkg-config --libs openssl 2>/dev/null)
CRYPTO_DEF    := $(shell pkg-config --exists openssl 2>/dev/null && echo -DHAVE_OPENSSL_SHA)

# Use libmicrohttpd and cJSON if present; otherwise build a thin shim path
MHD_CFLAGS  := $(shell pkg-config --cflags libmicrohttpd 2>/dev/null)
MHD_LIBS    := $(shell pkg-config --libs   libmicrohttpd 2>/dev/null)

CFLAGS      := $(STD) $(WARN) $(OPT) -Iinclude -Isrc -Isrc/http/controllers $(SQLITE_CFLAGS) $(CURL_CFLAGS) $(CRYPTO_CFLAGS) $(CRYPTO_DEF) $(MHD_CFLAGS)
LDFLAGS     := $(SQLITE_LIBS) $(CURL_LIBS) $(CRYPTO_LIBS) $(MHD_LIBS) -lpthread

ifeq ($(OS),Windows_NT)
LDFLAGS     += -lws2_32
endif

# ---- sources ---------------------------------------------------------------
SRC := $(wildcard src/*.c) $(wildcard src/http/*.c) \
       $(wildcard src/http/controllers/*.c) \
       $(wildcard src/services/*.c) $(wildcard src/repositories/*.c)
OBJ := $(SRC:.c=.o)
BIN := build/sae
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

# ---- targets ---------------------------------------------------------------
.PHONY: all debug sanitize clean run tests install-deps install help

all: $(BIN)

help:
	@echo "make            Compila o executavel build/sae"
	@echo "make run        Compila e inicia o servidor"
	@echo "make tests      Compila e executa os testes"
	@echo "make debug      Compila com simbolos de depuracao"
	@echo "make sanitize   Compila com AddressSanitizer e UBSan"
	@echo "make install    Instala sae em $(BINDIR)"
	@echo "make clean      Remove os arquivos gerados"

debug: CFLAGS += $(DBG)
debug: clean $(BIN)

sanitize: CFLAGS += $(DBG) $(SAN)
sanitize: LDFLAGS += $(SAN)
sanitize: clean $(BIN)

$(BIN): $(OBJ)
	@mkdir -p build
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) servidor

install: $(BIN)
	@mkdir -p $(BINDIR)
	install -m 755 $(BIN) $(BINDIR)/sae
	@echo "SAE instalado em $(BINDIR)/sae"

TEST_BINS := $(patsubst tests/%.c,build/%,$(wildcard tests/test_*.c))
TEST_SRC  := $(wildcard tests/test_*.c)

tests: $(TEST_BINS)
	@for b in $(TEST_BINS); do echo "==> $$b"; ./$$b || exit 1; done

# Cada teste declara as fontes da aplicação das quais depende.
build/test_validation: tests/test_validation.c src/validation.c
build/test_error:      tests/test_error.c      src/error.c
build/test_logger:     tests/test_logger.c     src/logger.c
build/test_database:   tests/test_database.c   src/database.c src/logger.c src/validation.c
build/test_auth:       tests/test_auth.c       src/database.c src/users.c src/auth.c src/validation.c src/logger.c
build/test_permissions: tests/test_permissions.c src/database.c src/users.c src/classes.c src/permissions.c src/logger.c src/validation.c
build/test_tasks:      tests/test_tasks.c      src/database.c src/users.c src/classes.c src/tasks.c src/logger.c src/validation.c
build/test_whatsapp:   tests/test_whatsapp.c   src/whatsapp.c src/validation.c src/logger.c
build/test_ai:         tests/test_ai.c         src/ai.c       src/validation.c src/logger.c
build/test_http:       tests/test_http.c       src/http/server.c src/http/router.c \
                       src/http/controllers/whatsapp_controller.c \
                       src/whatsapp.c src/whatsapp_adapter.c src/logger.c src/validation.c

build/test_%: tests/test_%.c
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

install-deps:
	@echo "Debian/Ubuntu/Pop!_OS:  sudo apt install -y build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev"
	@echo "Fedora:                  sudo dnf install -y gcc make pkg-config sqlite-devel libcurl-devel openssl-devel"
	@echo "Arch:                    sudo pacman -S --needed base-devel pkgconf sqlite curl openssl"

clean:
	rm -f $(OBJ) $(BIN)
	rm -f build/test_*
	@echo "Limpo."
