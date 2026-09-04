# =============================================================================
#  School System Backend - Makefile
#  Linguagem: C11 | Compilador: gcc (clang compatível)
# =============================================================================

CC          ?= cc
.DEFAULT_GOAL := all
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

# Quando libcurl estiver instalada, o build padrao ja inclui o adaptador
# oficial do WhatsApp. Sem libcurl, o SAE ainda compila em modo local.
ifneq ($(strip $(CURL_LIBS)),)
CFLAGS      += -DWHATSAPP_USE_CURL
endif

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
.PHONY: all debug sanitize whatsapp clean run tests install-deps install help FORCE

# A flag de WhatsApp depende da disponibilidade do libcurl. Forcamos a
# recompilacao dos objetos para nunca misturar objetos de modos diferentes.
FORCE:
$(OBJ): FORCE

all: $(BIN) install

help:
	@echo "make            Compila e atualiza o comando global sae"
	@echo "make run        Compila e inicia o servidor"
	@echo "make tests      Compila e executa os testes"
	@echo "make debug      Compila com simbolos de depuracao"
	@echo "make sanitize   Compila com AddressSanitizer e UBSan"
	@echo "make whatsapp   Igual a make (mantido por compatibilidade)"
	@echo "make install    Instala sae em $(BINDIR)"
	@echo "make clean      Remove os arquivos gerados"

debug: CFLAGS += $(DBG)
debug: clean $(BIN)

sanitize: CFLAGS += $(DBG) $(SAN)
sanitize: LDFLAGS += $(SAN)
sanitize: clean $(BIN)

whatsapp: all

$(BIN): $(OBJ)
	@mkdir -p build
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) servidor

install: $(BIN)
	@mkdir -p $(BINDIR)
	@ln -sf "$(abspath $(BIN))" "$(BINDIR)/sae"
	@echo "SAE vinculado a $(abspath $(BIN))"

TEST_BINS := $(patsubst testes/%.c,build/%,$(wildcard testes/test_*.c))
TEST_SRC  := $(wildcard testes/test_*.c)

tests: $(TEST_BINS)
	@for b in $(TEST_BINS); do echo "==> $$b"; ./$$b || exit 1; done

# Cada teste declara as fontes da aplicação das quais depende.
build/test_validation: testes/test_validation.c src/validation.c
build/test_error:      testes/test_error.c      src/error.c
build/test_logger:     testes/test_logger.c     src/logger.c
build/test_database:   testes/test_database.c   src/database.c src/logger.c src/validation.c
build/test_auth:       testes/test_auth.c       src/database.c src/users.c src/auth.c src/validation.c src/logger.c
build/test_permissions: testes/test_permissions.c src/database.c src/users.c src/classes.c src/permissions.c src/logger.c src/validation.c
build/test_tasks:      testes/test_tasks.c      src/database.c src/users.c src/classes.c src/tasks.c src/logger.c src/validation.c
build/test_whatsapp:   testes/test_whatsapp.c   src/whatsapp.c src/validation.c src/logger.c
build/test_ai:         testes/test_ai.c         src/ai.c       src/validation.c src/logger.c
build/test_http:       testes/test_http.c       src/http/server.c src/http/router.c src/config.c \
                       src/http/controllers/whatsapp_controller.c \
                       src/whatsapp.c src/whatsapp_adapter.c src/logger.c src/validation.c

build/test_%: testes/test_%.c
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

install-deps:
	@echo "Debian/Ubuntu/Pop!_OS:  sudo apt install -y build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev"
	@echo "Fedora:                  sudo dnf install -y gcc make pkg-config sqlite-devel libcurl-devel openssl-devel"
	@echo "Arch:                    sudo pacman -S --needed base-devel pkgconf sqlite curl openssl"
	@echo "MSYS2 UCRT64:            pacman -S --needed mingw-w64-ucrt-x86_64-curl"

clean:
	rm -f $(OBJ) $(BIN)
	rm -f build/test_*
	@echo "Limpo."
