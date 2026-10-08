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
| `WHATSAPP_API_URL` | vazio | URL da API do WhatsApp. |
| `WHATSAPP_API_TOKEN` | vazio | Token da API do WhatsApp. |
| `WHATSAPP_PHONE_NUMBER_ID` | vazio | Identificador do número WhatsApp. |
| `WHATSAPP_VERIFY_TOKEN` | vazio | Token de verificação do webhook. |
| `WHATSAPP_APP_SECRET` | vazio | Obrigatório para receber POSTs do webhook; valida a assinatura `X-Hub-Signature-256`. |
| `AI_API_URL` | `https://api.anthropic.com/v1/messages` | Endpoint Messages API compatível com Anthropic. |
| `AI_API_KEY` | vazio | Chave privada da API. Sem chave usa o modo local; nunca coloque no front-end nem no Git. |
| `AI_MODEL` | vazio | Identificador do modelo disponibilizado pelo provedor. |

Exemplo mínimo:

```dotenv
SERVER_PORT=8080
WEB_ROOT=../../ScienceFair2026
SESSION_TIMEOUT_MIN=60
LOG_LEVEL=INFO
```

### Ativar a integração de IA

Copie `.env.example` para `.env` e preencha `AI_API_KEY` e `AI_MODEL`. O endpoint padrão
é a API Messages da Anthropic; `AI_API_URL` pode apontar para um gateway compatível.
Quando libcurl está instalado, `make` habilita o cliente remoto automaticamente.
Sem a chave ou o modelo, o servidor mantém o comportamento local de demonstração.
As chamadas remotas extraem e validam os campos da tarefa antes de pedir confirmação
ao professor, e geram orientação ao aluno usando apenas o contexto escolar fornecido.
O servidor nunca envia a chave ao navegador. Áudio e imagens continuam dependendo dos
adaptadores próprios de transcrição e visão.

```bash
cp .env.example .env
# Edite .env e informe AI_API_KEY e AI_MODEL
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
