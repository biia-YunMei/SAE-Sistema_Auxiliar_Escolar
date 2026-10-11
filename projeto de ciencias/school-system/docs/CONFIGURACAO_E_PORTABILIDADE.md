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

Compile com `make`. O resultado padrão é `~/.cache/school-system-artifacts/sae` no Linux e
`~/.cache/school-system-artifacts/sae.exe` no Windows. Objetos e binários de testes
também são guardados nessa pasta irmã, fora do repositório. Para escolher outro
local, execute `make ARTIFACT_DIR=/caminho/desejado`. No NixOS, use o
`flake.nix` do repositório para abrir o ambiente de desenvolvimento antes de
executar `make`.

## Variáveis de ambiente

O sistema lê variáveis do ambiente e, se existir, do arquivo `.env` na raiz do
projeto. O `.env` não deve ser enviado ao Git.

| Variável | Padrão | Uso |
|---|---|---|
| `SERVER_HOST` | `0.0.0.0` | Endereço do servidor HTTP. |
| `SERVER_PORT` | `8080` | Porta HTTP. |
| `WEB_ROOT` | `../../ScienceFair2026` | Diretório das páginas estáticas. Relativo à pasta de execução do servidor. |
| `DATABASE_PATH` | `data/school.db` | Arquivo SQLite. |
| `LOG_DIR` | `logs` | Diretório de logs. |
| `LOG_LEVEL` | `INFO` | `DEBUG`, `INFO`, `WARN` ou `ERROR`. |
| `SESSION_TIMEOUT_MIN` | `60` | Duração da sessão em minutos. |
| `AI_PROVIDER` | `gemini` | Provedor usado pelo cliente C. |
| `AI_API_URL` | Gemini API | Base URL da API de geração do Gemini. |
| `AI_API_KEY` | vazio | Chave privada do Gemini; somente no ambiente local do back-end. |
| `AI_MODEL` | `gemini-3.8-flash` | Modelo de IA usado. |
| `AI_INSTRUCTIONS_FILE` | busca automática | Caminho opcional para o Markdown com instruções pedagógicas da IA. O arquivo é lido a cada resposta e limitado a 3500 bytes. |
| `ESCOLA_API_KEY` | vazio | Chave da API escolar para consultar inbox e responder mensagens. |
| `ESCOLA_CURSOR_PATH` | `data/escola_api.cursor` | Cursor local de mensagens já consultadas. |
| `SAE_BOOTSTRAP_KEY` | vazio | Chave temporária para o primeiro cadastro; mínimo 32 caracteres. O SAE remove a atribuição do `.env` após criar o primeiro usuário ou ao detectar usuários existentes. |

Exemplo mínimo:

```dotenv
SERVER_PORT=8080
WEB_ROOT=../../ScienceFair2026
SESSION_TIMEOUT_MIN=60
LOG_LEVEL=INFO
AI_PROVIDER=gemini
AI_API_URL=https://generativelanguage.googleapis.com/v1beta/models
AI_MODEL=gemini-3.8-flash
AI_API_KEY=
AI_INSTRUCTIONS_FILE=
ESCOLA_API_KEY=
SAE_BOOTSTRAP_KEY=
```

### Ativar a integração de IA

Copie `.env.example` para `.env` e preencha `AI_API_KEY` e `ESCOLA_API_KEY` localmente.
O projeto usa Gemini para dúvidas por texto. O arquivo `instruções/AIconfig.md` é carregado como orientação pedagógica; na ausência de `AI_INSTRUCTIONS_FILE`, o SAE procura esse arquivo nos caminhos relativos conhecidos. O conteúdo é limitado a 3500 bytes e lido a cada resposta. Se não for encontrado, as regras pedagógicas embutidas continuam ativas. O worker consulta a API escolar e responde
por ela; não requer webhook, túnel ou credenciais da Meta. Com libcurl disponível,
`make` inclui o cliente remoto. Sem chave ou libcurl, as respostas de IA ficam indisponíveis.
O servidor nunca envia essas chaves ao navegador.

```bash
cp .env.example .env
# Preencha AI_API_KEY e ESCOLA_API_KEY; use SAE_BOOTSTRAP_KEY no primeiro cadastro
make
sae servidor
```

Com o servidor ativo, abra `http://127.0.0.1:8080/` para usar o site e
`http://127.0.0.1:8080/setup.html` para o cadastro inicial. As páginas usam a
API na mesma origem; não abra os HTML diretamente com `file://`.

## Arquivos locais ignorados pelo Git

O `.gitignore` ignora binários, objetos, logs, arquivos SQLite/WAL, mídia,
sessão `.sae-session` e arquivos `.env`. Arquivos que já estavam rastreados
antes da criação do `.gitignore` continuam rastreados até serem removidos
explicitamente do índice do Git.
