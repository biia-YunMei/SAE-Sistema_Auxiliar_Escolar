# =============================================================================
#  School System Backend - Makefile
#  Linguagem: C11 | Compilador: gcc (clang compatível)
# =============================================================================

CC          ?= cc
.DEFAULT_GOAL := all

# O wrapper do Nix injeta NIX_LDFLAGS no compilador. Quando o projeto está
# em um diretório com espaços, essa variável chega ao linker sem escape e
# separa o caminho em vários argumentos. As dependências deste projeto já
# são obtidas via pkg-config, portanto removemos apenas esse caso quebrado.
ifneq (,$(findstring $(CURDIR),$(NIX_LDFLAGS)))
unexport NIX_LDFLAGS
endif

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
# O front-end fica fixado no binário, sem depender do diretório de execução nem
# de valores antigos no ambiente/.env. Para outra pasta: make SAE_WEB_ROOT=/abs/path
SAE_WEB_ROOT ?= $(abspath ../../ScienceFair2026)
CFLAGS      += -DSAE_DEFAULT_WEB_ROOT='"$(subst \,/,$(SAE_WEB_ROOT))"'
LDFLAGS     := $(SQLITE_LIBS) $(CURL_LIBS) $(CRYPTO_LIBS) $(MHD_LIBS)

# Quando libcurl estiver instalada, o build padrao ja inclui o adaptador
# oficial do WhatsApp. Sem libcurl, o SAE ainda compila em modo local.
ifneq ($(strip $(CURL_LIBS)),)
CFLAGS      += -DWHATSAPP_USE_CURL
CFLAGS      += -DAI_USE_REMOTE
endif

ifeq ($(OS),Windows_NT)
BIN_NAME     := sae.exe
CFLAGS      += -pthread
LDFLAGS     += -pthread -lws2_32
else
BIN_NAME     := sae
CFLAGS      += -pthread
LDFLAGS     += -pthread
endif

# ---- sources ---------------------------------------------------------------
SRC := $(wildcard src/*.c) $(wildcard src/http/*.c) \
       $(wildcard src/http/controllers/*.c) \
       $(wildcard src/services/*.c) $(wildcard src/repositories/*.c)
# Artefatos nunca ficam junto do código-fonte. Por padrão a pasta é irmã do
# projeto e, portanto, não entra no repositório Git. É possível substituí-la:
#   make ARTIFACT_DIR=/caminho/para/meus-artefatos
ARTIFACT_DIR ?= $(HOME)/.cache/school-system-artifacts
OBJ_DIR      := $(ARTIFACT_DIR)/obj
TEST_DIR     := $(ARTIFACT_DIR)/tests
BIN          := $(ARTIFACT_DIR)/$(BIN_NAME)
OBJ          := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRC))
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

# ---- targets ---------------------------------------------------------------
.PHONY: all debug sanitize whatsapp clean run tests install-deps install help FORCE

# A flag de WhatsApp depende da disponibilidade do libcurl. Forcamos a
# recompilacao dos objetos para nunca misturar objetos de modos diferentes.
FORCE:
$(OBJ): FORCE

ifeq ($(OS),Windows_NT)
all: $(BIN) install tests

install: $(BIN)
	@echo "Executável criado em $(BIN). Adicione $(ARTIFACT_DIR) ao PATH para usar sae."
else
all: $(BIN) install tests

install: $(BIN)
	@mkdir -p $(BINDIR)
	@ln -sf "$(BIN)" "$(BINDIR)/sae"
	@echo "SAE vinculado a $(BIN)"
endif

help:
	@echo "make            Compila e atualiza o comando global sae"
	@echo "                Também executa todos os testes automatizados"
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
	@mkdir -p "$(@D)"
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p "$(@D)"
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	"$(BIN)" servidor

TEST_BINS := $(patsubst testes/%.c,$(TEST_DIR)/%,$(wildcard testes/test_*.c))
TEST_SRC  := $(wildcard testes/test_*.c)

tests: $(TEST_BINS)
	@for b in $(TEST_BINS); do echo "==> $$b"; "$$b" || exit 1; done

# Cada teste declara as fontes da aplicação das quais depende.
$(TEST_DIR)/test_validation: testes/test_validation.c src/validation.c
$(TEST_DIR)/test_error:      testes/test_error.c      src/error.c
$(TEST_DIR)/test_json_util:  testes/test_json_util.c  src/json_util.c
$(TEST_DIR)/test_logger:     testes/test_logger.c     src/logger.c
$(TEST_DIR)/test_database:   testes/test_database.c   src/database.c src/logger.c src/validation.c
$(TEST_DIR)/test_auth:       testes/test_auth.c       src/database.c src/users.c src/auth.c src/validation.c src/logger.c
$(TEST_DIR)/test_permissions: testes/test_permissions.c src/database.c src/users.c src/classes.c src/permissions.c src/logger.c src/validation.c
$(TEST_DIR)/test_tasks:      testes/test_tasks.c      src/database.c src/users.c src/classes.c src/tasks.c src/logger.c src/validation.c
$(TEST_DIR)/test_feira:      testes/test_feira.c src/whatsapp.c src/anonymous_enrollment.c \
                       src/validation.c src/logger.c src/database.c src/users.c \
                       src/classes.c src/tasks.c src/ai.c
$(TEST_DIR)/test_feira: CFLAGS := $(filter-out -DWHATSAPP_USE_CURL -DAI_USE_REMOTE,$(CFLAGS))
.PHONY: test-feira
test-feira: $(TEST_DIR)/test_feira
	"$(TEST_DIR)/test_feira"
$(TEST_DIR)/test_whatsapp:   testes/test_whatsapp.c   src/whatsapp.c src/validation.c src/logger.c \
                       src/database.c src/users.c src/classes.c src/tasks.c src/ai.c \
                       src/anonymous_enrollment.c
$(TEST_DIR)/test_whatsapp: CFLAGS := $(filter-out -DWHATSAPP_USE_CURL -DAI_USE_REMOTE,$(CFLAGS))
$(TEST_DIR)/test_ai:         CFLAGS := $(filter-out -DAI_USE_REMOTE,$(CFLAGS))
$(TEST_DIR)/test_ai:         testes/test_ai.c         src/ai.c       src/validation.c src/logger.c
$(TEST_DIR)/test_http:       testes/test_http.c       src/http/server.c src/http/router.c src/config.c \
                       src/http/controllers/whatsapp_controller.c \
                       src/whatsapp.c src/whatsapp_adapter.c src/auth.c src/database.c \
                       src/users.c src/classes.c src/tasks.c src/ai.c src/logger.c src/validation.c \
                       src/anonymous_enrollment.c src/json_util.c
$(TEST_DIR)/test_students:   testes/test_students.c src/http/controllers/students_controller.c \
                       src/http/controllers/bootstrap_controller.c src/http/controllers/users_controller.c \
                       src/http/controllers/teachers_controller.c src/http/controllers/classes_controller.c \
                       src/http/controllers/notices_controller.c \
                       src/auth.c src/config.c src/database.c src/users.c src/classes.c src/validation.c src/logger.c \
                       src/json_util.c

$(TEST_DIR)/test_%: testes/test_%.c
	@mkdir -p "$(@D)"
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

install-deps:
	@echo "Debian/Ubuntu/Pop!_OS:  sudo apt install -y build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev"
	@echo "Fedora:                  sudo dnf install -y gcc make pkg-config sqlite-devel libcurl-devel openssl-devel"
	@echo "Arch:                    sudo pacman -S --needed base-devel pkgconf sqlite curl openssl"
	@echo "MSYS2 UCRT64:            pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,make,pkgconf,sqlite3,curl,openssl}"

clean:
	@case "$(ARTIFACT_DIR)" in ''|/|.) echo "Diretório de artefatos inválido."; exit 1;; esac
	rm -rf "$(ARTIFACT_DIR)"
	@echo "Artefatos removidos de $(ARTIFACT_DIR)."
