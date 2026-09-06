# SAE — Sistema Auxiliar Escolar (C11)

Back-end escrito **inteiramente em C** de um sistema escolar integrado ao
WhatsApp, com coordenação, professores, alunos, banco de dados e
inteligência artificial. Desenvolvido para apresentação em feira de
ciências.

> ✅ **10 etapas concluídas** — base, banco, usuários, tarefas, API HTTP,
> WhatsApp, IA, conversas, áudio/imagem, segurança/testes.

---

## 1. Objetivo

Núcleo do sistema que:

* Cadastra e gerencia usuários (coordenador, professor, aluno).
* Controla permissões e turmas.
* Recebe e envia mensagens via WhatsApp.
* Processa linguagem natural com IA (modo local heurístico + modo remoto opcional).
* Armazena tarefas, provas, trabalhos e comunicados.
* Expõe API HTTP para um futuro painel administrativo.

## 2. Arquitetura

```
┌──────────────────────────────┐
│ HTTP/1.1 (sockets Linux/Windows) │ src/http/server.c
├──────────────────────────────┤
│ Roteador + controllers       │   src/http/{router,controllers}/*.c
├──────────────────────────────┤
│ Services (regras)            │   src/services/*
├──────────────────────────────┤
│ Repositories (acesso dados)  │   src/repositories/*
├──────────────────────────────┤
│ SQLite (migrations, FK, WAL) │   src/database.c

Módulos isolados (não conhecem o banco):
  src/whatsapp.c      → abstração do canal
  src/whatsapp_adapter.c → libcurl (opt-in via -DWHATSAPP_USE_CURL)
  src/ai.c            → IA local/remota (opt-in via -DAI_USE_REMOTE)
  src/transcription.c → áudio
  src/image.c         → visão
  src/conversations.c → máquina de estados
```

## 3. Estrutura de Diretórios

```
school-system/
├── Makefile                # gcc, -Wall -Wextra -Wpedantic, alvo sanitize
├── README.md
├── .gitignore
├── docs/                   # uso, permissões, configuração e portabilidade
├── include/                # headers públicos (config, error, server, ai, ...)
├── src/
│   ├── main.c              # bootstrap: config/logger/db/whatsapp/http
│   ├── config.c / logger.c / error.c / validation.c
│   ├── database.c          # SQLite + migrations
│   ├── users.c / auth.c / permissions.c / classes.c
│   ├── tasks.c             # CRUD com regra professor↔turma
│   ├── whatsapp.c          # abstração
│   ├── whatsapp_adapter.c  # concreto via libcurl (opt-in)
│   ├── ai.c                # cliente IA
│   ├── conversations.c     # máquina de estados
│   ├── transcription.c / image.c
│   └── http/
│       ├── server.c / router.c
│       ├── controllers/{controllers.h, auth, users, classes, tasks, whatsapp, wrappers}.c
│       ├── repositories/*.c
│       └── services/*.c
├── data/                   # banco SQLite local (ignorado pelo Git)
├── logs/                   # logs locais (ignorados pelo Git)
└── tests/                  # test_validation, test_error, test_logger,
                            # test_database, test_auth, test_permissions,
                            # test_tasks, test_http, test_whatsapp, test_ai
```

Documentação complementar:

* [Uso e permissões](docs/USO_E_PERMISSOES.md)
* [Configuração e portabilidade](docs/CONFIGURACAO_E_PORTABILIDADE.md)

## 4. Dependências e plataformas

| Obrigatória | Função |
|---|---|
| libc + pthreads | base |

| Opcionais (ativam módulos concretos) | Como ativar |
|---|---|
| libsqlite3-dev | `pkg-config --cflags sqlite3` em Makefile (auto-detect) |
| libcurl-dev | `-DWHATSAPP_USE_CURL` e/ou `-DAI_USE_REMOTE` |
| libmicrohttpd-dev | — (não usamos; HTTP em C puro) |
| libcjson-dev | — (não usamos; parser JSON mínimo próprio) |

### Linux — Ubuntu, Debian, Linux Mint e Pop!_OS

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev libssl-dev
make tests
make sanitize
make
```

O binário resultante é `build/sae`. As bibliotecas cURL e OpenSSL
são detectadas automaticamente pelo `pkg-config`; o sistema continua compilando
sem cURL quando os adaptadores remotos não forem habilitados.

### Outras distribuições Linux

```bash
# Fedora
sudo dnf install -y gcc make pkg-config sqlite-devel libcurl-devel openssl-devel

# Arch Linux / Manjaro
sudo pacman -S --needed base-devel pkgconf sqlite curl openssl
```

No NixOS, entre no ambiente de desenvolvimento definido pelo projeto e execute
`make`. O Makefile também evita que caminhos com espaços sejam quebrados pelos
parâmetros do linker do Nix.

### Windows

Use o terminal **MSYS2 UCRT64**:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-sqlite3 \
  mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-openssl
make
```

O executável será `build/sae.exe`. Adicione a pasta `build` ao `PATH` para usar
o comando `sae` em qualquer diretório.

## 5. Compilação

```bash
make            # release (auto-detecta libs instaladas)
make debug      # com -g -DDEBUG
make sanitize   # com AddressSanitizer + UBSan
make tests      # roda todos os test_*
make clean
```

Para ativar o adapter WhatsApp/IA remoto:

```bash
make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -DWHATSAPP_USE_CURL -DAI_USE_REMOTE' \
     LDFLAGS='-lcurl'
```

## 6. Execução e primeiros passos

```bash
# Crie .env apenas se precisar alterar configurações ou integrar serviços externos
make
sae servidor
```

Sinais `SIGINT`/`SIGTERM` finalizam o servidor limpando recursos.

Em outro terminal, crie o primeiro coordenador. Este é o único comando que
pode ser usado sem uma sessão iniciada:

```bash
sae -ca -n "Nome do coordenador" -p +5566999999999 -s sua_senha
sae login -p +5566999999999 -s sua_senha
```

Depois do login, a sessão fica salva localmente em `.sae-session`. Use
`sae logout` ao terminar.

## 7. Comandos do terminal

Use `sae --help` para consultar todos os comandos. Os principais são:

```bash
# Usuários — somente COORD
sae usuario criar -n NOME -p TELEFONE -t COORD|PROF|STUD [-s SENHA]
sae usuario listar
sae usuario editar -u ID -n NOME -p TELEFONE [-s SENHA]
sae usuario excluir -u ID

# Turmas — COORD e PROF
sae turma criar -n NOME -g SERIE [-t MANHA|TARDE|NOITE|INTEGRAL]
sae turma listar
sae turma editar -i ID -n NOME -g SERIE -t MANHA|TARDE|NOITE|INTEGRAL
sae turma excluir -i ID
sae turma vincular -c ID_TURMA -u ID_USUARIO_PROF  # somente COORD

# Alunos — COORD ou professor vinculado à turma
sae aluno vincular -u ID_ALUNO -c ID_TURMA

# Tarefas — COORD e PROF
sae tarefa criar -c ID_TURMA -m MATERIA -n TITULO -v AAAA-MM-DD [-d DESCRICAO]
sae tarefa criar -c ID_TURMA -u ID_PROF -m MATERIA -n TITULO -v AAAA-MM-DD
sae tarefa listar -c ID_TURMA
sae tarefa editar -i ID -m MATERIA -n TITULO -v AAAA-MM-DD [-d DESCRICAO]
sae tarefa excluir -i ID

# WhatsApp — COORD e PROF
sae whatsapp enviar -p TELEFONE -m MENSAGEM
sae whatsapp turma -c ID_TURMA -m MENSAGEM
```

O professor só pode administrar turmas vinculadas a ele e tarefas sob sua
responsabilidade. O aluno só pode listar tarefas da própria turma. Todos os
cargos podem usar `sae logout` e `sae --help`.

### Visual do terminal

Em terminais interativos com UTF-8, o SAE mostra molduras Unicode nas
listagens, mensagens de erro e confirmações. Em ambiente incompatível ou quando
a saída é redirecionada, usa texto simples automaticamente.

```bash
SAE_UI=unicode sae turma listar  # força as molduras Unicode
SAE_UI=plain sae turma listar    # força a saída simples
```

## 8. Endpoints

| Método | Caminho | Quem | Descrição |
|--------|---------|------|-----------|
| GET    | /api/health              | público | status |
| POST   | /api/auth                | público | login por telefone |
| POST   | /api/auth/logout         | logado | encerra a sessão |
| POST   | /api/bootstrap           | público | cria o primeiro coordenador |
| POST   | /api/users               | COORD   | cadastra usuário |
| GET    | /api/users               | COORD/PROF | lista usuários |
| PUT/DELETE | /api/users/:id        | COORD   | edita/exclui usuário |
| GET    | /api/classes             | COORD/PROF | lista turmas |
| POST   | /api/classes             | COORD/PROF | cria turma |
| PUT/DELETE | /api/classes/:id      | COORD/PROF vinculado | edita/exclui turma |
| POST   | /api/classes/:id/teachers| COORD   | vincula professor |
| DELETE | /api/classes/:id/teachers/:tid | COORD | remove vínculo |
| GET    | /api/tasks?class_id=X   | conforme turma/cargo | lista tarefas da turma |
| POST   | /api/tasks               | PROF/COORD | cria tarefa |
| PUT    | /api/tasks/:id           | PROF/COORD | atualiza tarefa |
| DELETE | /api/tasks/:id           | PROF/COORD | remove tarefa |
| POST   | /api/whatsapp/webhook    | externo | recebe mensagem |

## 9. Autenticação e permissões

* Login: `POST /api/auth` com `{phone, password}`.
* Token opaco (hex 64 chars) devolvido e usado em `Authorization: Bearer <token>`.
* Sessões em memória com TTL (`SESSION_TIMEOUT_MIN`).
* **Toda rota, exceto saúde, login, bootstrap e webhook, exige login.**
* **Toda verificação de permissão passa pelo back-end**, não apenas pelo CLI.

| Cargo | Pode executar |
|---|---|
| `COORD` | Todos os comandos administrativos e operacionais. |
| `PROF` | Listar usuários; criar/listar/editar/excluir suas turmas; vincular alunos em suas turmas; criar/listar/editar/excluir suas tarefas; enviar WhatsApp. |
| `STUD` | Listar apenas as tarefas da própria turma. |

## 10. Banco de Dados

SQLite com `PRAGMA foreign_keys=ON` e WAL. Migrations idempotentes em
`schema_version`. Tabelas principais:

* `users(id, name, phone, password_hash, user_type, status, created_at)`
* `classes(id, name, grade, shift, status)`
* `teachers`, `students`, `teacher_classes` (N:N)
* `tasks`, `exams`, `assignments`, `notices`
* `messages(auditoria)`, `conversations(state machine)`

## 11. Segurança

* **Todas** entradas validadas: telefone, data ISO, hora, comprimento,
  charset, IDs.
* Senhas: hash SHA-256 + salt (formato `salt:hash` em hex).
  README indica evolução para bcrypt/argon2.
* SQL parametrizado (`sqlite3_prepare_v2` em todas as queries).
* Limites: `MAX_MESSAGE_SIZE`, `MAX_REQUEST_SIZE`.
* Logs não contêm tokens, senhas ou conteúdo completo por padrão.
* Credenciais fora do código (env vars / `.env`).
* HTTPS/TLS é responsabilidade do reverse proxy em produção.

## 12. Fluxos Implementados

```
Coordenador (painel / API)
   ├── cadastra professor/aluno/turma
   └── vincula professor ↔ turma

Professor (WhatsApp)
   ├── envia mensagem
   ├── backend identifica (telefone)
   ├── IA extrai {turma, disciplina, descrição, data}
   ├── professor CONFIRMA
   └── tarefa gravada em `tasks`

Aluno (WhatsApp)
   ├── envia pergunta
   ├── backend identifica
   ├── busca contexto da turma
   ├── IA orienta por etapas (nunca entrega resposta final)
   └── resposta enviada pelo WhatsApp
```

## 13. Testes

```bash
make tests
```

Cobre:

* Validação (telefone/data/hora/comprimento)
* Erros (`error_string`)
* Logger
* Database (schema + prepared statements)
* Auth (hash, telefone, sessão)
* Permissões e turmas
* Tasks (CRUD + regra professor↔turma + edição negada a não-dono)
* HTTP server (saúde + 404)
* WhatsApp (validação, tamanho, eco)
* IA (parse, JSON, política anti-resposta-pronta)

## 14. Limitações Conhecidas

* Áudio/imagem ainda em stub (pipeline pronto para plugar serviços).
* Sem HTTPS embutido (use nginx/Caddy em produção).
* Sessões em memória (não sobrevivem a restart).
* Hash de senha didático (SHA-256+salt) — migrar para bcrypt em produção.

## 15. Roadmap (futuro)

* Persistir sessões (Redis/SQLite).
* Painel web administrativo usando os endpoints HTTP.
* Visão computacional para correção de exercícios.
* Integração bidirecional WhatsApp (status de mensagens).
* Migrations automáticas no `make`.
