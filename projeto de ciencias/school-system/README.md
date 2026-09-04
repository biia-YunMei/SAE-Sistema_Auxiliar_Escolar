# School System Backend (C11)

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
│ HTTP/1.1 (POSIX sockets)    │   src/http/server.c
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
├── .env.example
├── .gitignore
├── ARCHITECTURE.md         # detalhamento
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
├── data/                   # school.db
├── logs/                   # school-YYYYMMDD.log
└── tests/                  # test_validation, test_error, test_logger,
                            # test_database, test_auth, test_permissions,
                            # test_tasks, test_http, test_whatsapp, test_ai
```

## 4. Dependências

| Obrigatória | Função |
|---|---|
| libc + pthreads | base |

| Opcionais (ativam módulos concretos) | Como ativar |
|---|---|
| libsqlite3-dev | `pkg-config --cflags sqlite3` em Makefile (auto-detect) |
| libcurl-dev | `-DWHATSAPP_USE_CURL` e/ou `-DAI_USE_REMOTE` |
| libmicrohttpd-dev | — (não usamos; HTTP em C puro) |
| libcjson-dev | — (não usamos; parser JSON mínimo próprio) |

### Ubuntu, Debian, Linux Mint e Pop!_OS

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev libssl-dev
make tests
make sanitize
make
```

O binário resultante é `build/school-backend`. As bibliotecas cURL e OpenSSL
são detectadas automaticamente pelo `pkg-config`; o sistema continua compilando
sem cURL quando os adaptadores remotos não forem habilitados.

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

## 6. Execução

```bash
cp .env.example .env       # edite credenciais
make
./build/school-backend
```

Sinais `SIGINT`/`SIGTERM` finalizam o servidor limpando recursos.

## 7. Endpoints

| Método | Caminho | Quem | Descrição |
|--------|---------|------|-----------|
| GET    | /api/health              | público | status |
| POST   | /api/auth                | público | login por telefone |
| POST   | /api/users               | COORD   | cadastra usuário |
| GET    | /api/users               | COORD   | lista usuários |
| GET    | /api/classes             | logado  | lista turmas |
| POST   | /api/classes             | COORD   | cria turma |
| POST   | /api/classes/:id/teachers| COORD   | vincula professor |
| DELETE | /api/classes/:id/teachers/:tid | COORD | remove vínculo |
| GET    | /api/tasks?class_id=X   | logado  | lista tarefas da turma |
| POST   | /api/tasks               | PROF/COORD | cria tarefa |
| PUT    | /api/tasks/:id           | PROF/COORD | atualiza tarefa |
| DELETE | /api/tasks/:id           | PROF/COORD | remove tarefa |
| POST   | /api/whatsapp/webhook    | externo | recebe mensagem |

## 8. Autenticação

* Login: `POST /api/auth` com `{phone, password}`.
* Token opaco (hex 64 chars) devolvido e usado em `Authorization: Bearer <token>`.
* Sessões em memória com TTL (`SESSION_TIMEOUT_MIN`).
* **Toda verificação de permissão passa pelo back-end.**

## 9. Banco de Dados

SQLite com `PRAGMA foreign_keys=ON` e WAL. Migrations idempotentes em
`schema_version`. Tabelas principais:

* `users(id, name, phone, password_hash, user_type, status, created_at)`
* `classes(id, name, grade, shift, status)`
* `teachers`, `students`, `teacher_classes` (N:N)
* `tasks`, `exams`, `assignments`, `notices`
* `messages(auditoria)`, `conversations(state machine)`

## 10. Segurança

* **Todas** entradas validadas: telefone, data ISO, hora, comprimento,
  charset, IDs.
* Senhas: hash SHA-256 + salt (formato `salt:hash` em hex).
  README indica evolução para bcrypt/argon2.
* SQL parametrizado (`sqlite3_prepare_v2` em todas as queries).
* Limites: `MAX_MESSAGE_SIZE`, `MAX_REQUEST_SIZE`.
* Logs não contêm tokens, senhas ou conteúdo completo por padrão.
* Credenciais fora do código (env vars / `.env`).
* HTTPS/TLS é responsabilidade do reverse proxy em produção.

## 11. Fluxos Implementados

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

## 12. Testes

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

## 13. Limitações Conhecidas

* Áudio/imagem ainda em stub (pipeline pronto para plugar serviços).
* Sem HTTPS embutido (use nginx/Caddy em produção).
* Sessões em memória (não sobrevivem a restart).
* Hash de senha didático (SHA-256+salt) — migrar para bcrypt em produção.

## 14. Roadmap (futuro)

* Persistir sessões (Redis/SQLite).
* Painel web administrativo usando os endpoints HTTP.
* Visão computacional para correção de exercícios.
* Integração bidirecional WhatsApp (status de mensagens).
* Migrations automáticas no `make`.
