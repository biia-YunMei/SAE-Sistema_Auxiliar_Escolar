# Configuração e portabilidade

## Plataformas suportadas

O código usa C11 e possui caminhos específicos para Linux e Windows. No Linux,
funciona com GCC ou Clang; no Windows, use MSYS2 UCRT64 com MinGW-w64.

```bash
# Debian, Ubuntu, Mint e Pop!_OS
sudo apt install -y build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev libssl-dev

# Fedora
sudo dnf install -y gcc make pkg-config sqlite-devel libcurl-devel openssl-devel

# Arch Linux / Manjaro
sudo pacman -S --needed base-devel pkgconf sqlite curl openssl

# Windows — terminal MSYS2 UCRT64
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-sqlite3 \
  mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-openssl
```

Compile com `make`. O resultado é `build/sae` no Linux e `build/sae.exe` no
Windows. No NixOS, use o `flake.nix` do repositório para abrir o ambiente de
desenvolvimento antes de executar `make`.

## Variáveis de ambiente

O sistema lê variáveis do ambiente e, se existir, do arquivo `.env` na raiz do
projeto. O `.env` não deve ser enviado ao Git.

| Variável | Padrão | Uso |
|---|---|---|
| `SERVER_HOST` | `0.0.0.0` | Endereço do servidor HTTP. |
| `SERVER_PORT` | `8080` | Porta HTTP. |
| `DATABASE_PATH` | `data/school.db` | Arquivo SQLite. |
| `LOG_DIR` | `logs` | Diretório de logs. |
| `LOG_LEVEL` | `INFO` | `DEBUG`, `INFO`, `WARN` ou `ERROR`. |
| `SESSION_TIMEOUT_MIN` | `60` | Duração da sessão em minutos. |
| `WHATSAPP_API_URL` | vazio | URL da API do WhatsApp. |
| `WHATSAPP_API_TOKEN` | vazio | Token da API do WhatsApp. |
| `WHATSAPP_PHONE_NUMBER_ID` | vazio | Identificador do número WhatsApp. |
| `WHATSAPP_VERIFY_TOKEN` | vazio | Token de verificação do webhook. |
| `WHATSAPP_APP_SECRET` | vazio | Segredo para validar o webhook. |

Exemplo mínimo:

```dotenv
SERVER_PORT=8080
SESSION_TIMEOUT_MIN=60
LOG_LEVEL=INFO
```

## Arquivos locais ignorados pelo Git

O `.gitignore` ignora binários, objetos, logs, arquivos SQLite/WAL, mídia,
sessão `.sae-session` e arquivos `.env`. Arquivos que já estavam rastreados
antes da criação do `.gitignore` continuam rastreados até serem removidos
explicitamente do índice do Git.
