# SAE — Sistema Auxiliar Escolar (C11)

Back-end escrito **inteiramente em C** de um sistema escolar integrado ao
WhatsApp, com coordenação, professores, alunos, banco de dados e
inteligência artificial. Desenvolvido para apresentação em feira de
ciências.

> O núcleo C/SQLite, o painel, as regras de acesso e os fluxos de tarefas e comunicados estão implementados. Integrações externas e alguns itens de produção continuam pendentes; veja [Limitações conhecidas](#14-limitações-conhecidas) e [Próximas etapas](#15-próximas-etapas).

---

## 1. Objetivo

Núcleo do sistema que:

* Cadastra e gerencia usuários (coordenador, professor, aluno).
* Controla permissões e turmas.
* Consulta a inbox da API escolar e responde mensagens recebidas pelo ID original. Não inicia conversa com números arbitrários.
* Processa perguntas com IA remota opcional e validação pelo back-end.
* Armazena tarefas, provas, trabalhos e comunicados.
* Expõe API HTTP integrada ao painel web em `../../ScienceFair2026`.

## 2. Arquitetura

```
┌──────────────────────────────┐
│ Painel HTML/JS               │ ../../ScienceFair2026
├──────────────────────────────┤
│ API HTTP + controllers       │ src/http/{server,router,controllers}/*.c
├──────────────────────────────┤
│ Regras de negócio em C       │ src/*.c
├──────────────────────────────┤
│ SQLite (migrations, FK, WAL) │ src/database.c

Módulos principais:
  src/whatsapp.c      → abstração do canal
  src/whatsapp_adapter.c → libcurl para adaptador de WhatsApp configurado
  src/escola_api.c       → inbox/reply da API escolar e worker automático
  src/ai.c               → Gemini remoto opcional via libcurl
  src/transcription.c    → interface local ainda sem provedor
  src/image.c            → interface local ainda sem provedor
  src/conversations.c → máquina de estados
```

## 3. Estrutura de Diretórios

```
school-system/
├── Makefile                  # build, testes e instalação do comando sae
├── README.md
├── docs/                     # configuração, portabilidade e permissões
├── include/                  # interfaces públicas dos módulos
├── src/
│   ├── main.c                # inicialização da aplicação
│   ├── database.c            # SQLite e migrations
│   ├── auth.c / users.c       # autenticação e usuários
│   ├── classes.c / tasks.c    # turmas e tarefas
│   ├── escola_api.c           # inbox/reply e worker automático
│   ├── ai.c                   # cliente de IA
│   └── http/
│       ├── server.c / router.c
│       └── controllers/       # endpoints HTTP
├── testes/                   # testes automatizados
└── data/                     # banco e cursor locais (ignorados pelo Git)
```

Documentação complementar:

* [Uso e permissões](docs/USO_E_PERMISSOES.md)
* [Manual de WhatsApp para professores](docs/MANUAL_WHATSAPP_PROFESSORES.md)
* [Configuração e portabilidade](docs/CONFIGURACAO_E_PORTABILIDADE.md)

## 4. Dependências e plataformas

| Obrigatória | Função |
|---|---|
| libc + pthreads | base |

| Opcionais (ativam módulos concretos) | Como ativar |
|---|---|
| libsqlite3-dev | Obrigatória para persistência SQLite |
| libcurl-dev | Necessária para IA remota e integrações HTTP externas |
| libmicrohttpd-dev | — (não usamos; HTTP em C puro) |
| libcjson-dev | — (não usamos; parser JSON mínimo próprio) |

### Linux — Ubuntu, Debian, Linux Mint e Pop!_OS

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev libssl-dev
make
```

O binário resultante é `~/.cache/school-system-artifacts/sae`. As bibliotecas cURL e OpenSSL
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

O executável será `~/.cache/school-system-artifacts/sae.exe`. Adicione essa pasta ao `PATH` para usar
o comando `sae` em qualquer diretório.

## 5. Compilação

```bash
make            # compila, executa os testes e instala o comando sae
make debug      # com -g -DDEBUG
make sanitize   # com AddressSanitizer + UBSan
make tests      # roda todos os test_*
make clean
```

Por padrão, o executável, objetos e testes compilados são gerados em
`~/.cache/school-system-artifacts`, uma pasta fora do repositório. Para escolher
outro local, use `make ARTIFACT_DIR=/caminho/desejado`.

Com libcurl instalado, o worker de inbox/reply da API escolar e o cliente de IA
remota são incluídos automaticamente no build. Copie `.env.example` para `.env`
e informe localmente as chaves do Gemini e da API escolar:

```bash
cp .env.example .env
# Edite AI_API_KEY e ESCOLA_API_KEY dentro do .env
make
```

A tutoria carrega as regras pedagógicas de `~/instruçoes/AIconfig.md` quando o
arquivo está disponível. O Markdown é relido a cada pergunta e limitado a 6000
bytes; o log informa o caminho e o tamanho carregados. Para usar outro arquivo, defina `AI_INSTRUCTIONS_FILE`; na falta do
arquivo, o SAE mantém as regras de tutoria embutidas no código.

Sem as chaves, as integrações remotas ficam indisponíveis. Veja
[`docs/CONFIGURACAO_E_PORTABILIDADE.md`](docs/CONFIGURACAO_E_PORTABILIDADE.md)
para as variáveis de configuração.

## 6. Execução e primeiros passos

```bash
# Crie .env apenas se precisar alterar configurações ou integrar serviços externos
make
sae servidor
```

Com o servidor ativo, abra `http://127.0.0.1:8080/` no navegador. O front-end é
servido pelo próprio SAE; use `http://127.0.0.1:8080/setup.html` para o cadastro
inicial e não abra os arquivos HTML diretamente com `file://`.

Sinais `SIGINT`/`SIGTERM` finalizam o servidor limpando recursos.

Em outro terminal, crie o primeiro coordenador. Este é o único comando que
pode ser usado sem uma sessão iniciada:

```bash
sae -ca -n "Nome do coordenador" -p +5566999999999
sae login -p +5566999999999
```

O terminal solicita a senha sem exibi-la. Não passe senhas como argumentos do
comando, pois elas podem ficar no histórico do shell e visíveis na lista de processos.

Depois do login, a sessão fica salva localmente em `.sae-session`. Use
`sae logout` ao terminar.

## 7. Comandos do terminal

Use `sae --help` para consultar todos os comandos. Os principais são:

```bash
# Usuários — somente COORD
sae usuario criar -n NOME -p TELEFONE -t COORD|PROF|STUD
sae usuario listar
sae usuario editar -u ID -n NOME -p TELEFONE [--senha]
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

# WhatsApp — PROFESSOR autenticado
sae whatsapp vincular
# Remover o vínculo da conta autenticada de professor
sae whatsapp desvincular
# Responder uma mensagem recebida pela inbox
sae whatsapp enviar -i ID_DA_INBOX -m MENSAGEM
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
| POST   | /api/users               | COORD   | cadastra coordenador ou professor |
| GET    | /api/users               | COORD/PROF | lista usuários |
| PUT/DELETE | /api/users/:id        | COORD   | edita/exclui usuário |
| GET/POST | /api/students          | COORD   | lista/cadastra alunos sem criar login |
| POST   | /api/students/class      | COORD/PROF vinculado | vincula aluno a turma |
| GET    | /api/teachers            | COORD | lista professores |
| POST   | /api/teachers            | COORD | cadastra professor |
| DELETE | /api/teachers/:id        | COORD | remove professor |
| GET    | /api/classes             | COORD/PROF vinculado | lista turmas permitidas |
| POST   | /api/classes             | COORD | cria turma |
| PUT/DELETE | /api/classes/:id      | COORD | edita/inativa turma |
| POST   | /api/classes/:id/teachers| COORD   | vincula professor |
| DELETE | /api/classes/:id/teachers/:tid | COORD | remove vínculo |
| GET    | /api/tasks?class_id=X   | conforme turma/cargo | lista tarefas da turma |
| GET    | /api/tasks?mine=1       | PROF   | lista tarefas do professor autenticado |
| GET    | /api/tasks?all=1        | COORD  | lista todas as tarefas |
| POST   | /api/tasks               | PROF/COORD | cria tarefa |
| PUT    | /api/tasks/:id           | PROF/COORD | atualiza tarefa |
| DELETE | /api/tasks/:id           | PROF/COORD | remove tarefa |
| GET    | /api/notices             | COORD/PROF ou aluno autenticado | lista comunicados permitidos |
| POST   | /api/notices             | COORD | cria comunicado |
| PUT/DELETE | /api/notices/:id      | COORD | edita/remove comunicado |
| GET    | /api/messages            | COORD; PROF vê apenas mensagens de suas turmas | histórico; nome/telefone só aparecem quando esses dados existem no SAE |
| POST   | /api/whatsapp/teacher-link-code | PROF | gera código temporário de vínculo WhatsApp |
| DELETE | /api/whatsapp/teacher-link | PROF | remove o vínculo e descarta publicações ainda pendentes |

## 9. Autenticação e permissões

* Login: `POST /api/auth` com `{phone, password}`. Alunos são recusados e não possuem login.
* Token opaco (hex 64 chars) devolvido e usado em `Authorization: Bearer <token>`.
* Sessões em memória com TTL (`SESSION_TIMEOUT_MIN`).
* **Toda rota, exceto saúde, login e bootstrap, exige login.**
* **Toda verificação de permissão passa pelo back-end**, não apenas pelo CLI.

| Cargo | Pode executar |
|---|---|
| `COORD` | Todos os comandos administrativos e operacionais. |
| `PROF` | Listar turmas vinculadas; criar/listar/editar/excluir suas tarefas; consultar mensagens anônimas recebidas apenas das turmas vinculadas. |
| `STUD` | Sem login; nome, telefone e série são cadastrados pela coordenação. Consulta atividades pelo WhatsApp. |

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
   ├── entra na tela Professores e seleciona “Vincular meu WhatsApp” (ou usa `sae whatsapp vincular`)
   ├── envia `VINCULAR <código>` ao número da escola pela inbox
   ├── sistema associa o remetente pseudônimo ao usuário autenticado
   ├── IA interpreta tarefa; sistema valida turma e vínculo professor↔turma
   ├── professor confirma; tarefa é gravada em `tasks`
   ├── consulta suas tarefas com `MINHAS TAREFAS` e altera uma tarefa própria por ID
   ├── consulta lembretes publicados com `LEMBRETES`
   └── envia ou edita lembretes de suas turmas e confirma antes de publicar

Aluno (WhatsApp)
   ├── API escolar entrega `from_ref` pseudônimo, sem telefone
   ├── aluno escolhe a turma na consulta; o sistema não recebe telefone para identificar a pessoa
   ├── sistema busca contexto da turma
   ├── IA orienta por etapas (nunca entrega resposta final)
   └── resposta enviada pelo WhatsApp
```

O código de vínculo expira em 10 minutos, é de uso único e fica armazenado no
banco somente como hash. O remetente anônimo não pode publicar antes do vínculo de professor.
Na tela Professores, o botão “Vincular meu WhatsApp” gera e exibe o código; envie
`VINCULAR <código>` pelo WhatsApp do SAE antes de ele expirar.
Tarefas e recados são publicados para consulta quando o aluno pergunta; a API
escolar atual não permite iniciar conversa nem enviar notificações em massa.

Após vincular o WhatsApp, o professor pode enviar uma tarefa em linguagem
natural e confirmar a prévia com `SIM`. Para consultar IDs e editar uma tarefa
própria, use `MINHAS TAREFAS` e depois `EDITAR TAREFA <id> | título: ... |
matéria: ... | descrição: ... | entrega: AAAA-MM-DD`. Lembretes são avisos
associados a turmas: `LEMBRETE <turma>: <texto>` cria um, `LEMBRETES` lista os
ativos do professor e `EDITAR LEMBRETE <id>: <novo texto>` prepara uma edição.
As alterações só são gravadas após `SIM`; `CANCELAR` descarta o rascunho.

## 13. Testes

```bash
make            # build + suíte automatizada
make tests      # executa somente a suíte
```

Cobre:

* Validação (telefone/data/hora/comprimento)
* Erros (`error_string`)
* Logger
* Database (schema + prepared statements)
* Auth (hash, telefone, sessão)
* Permissões e turmas
* Tasks (CRUD + regra professor↔turma + edição negada a não-dono)
* HTTP (roteamento e respostas; a parte que abre sockets depende das permissões do ambiente)
* WhatsApp/feira (consultas por turma, código de vínculo de professor, publicação após confirmação e filtro de mensagens)
* IA (parse, JSON, política anti-resposta-pronta)

## 14. Limitações conhecidas

* `src/transcription.c` e `src/image.c` ainda não têm provedor local e retornam `ERROR_NOT_IMPLEMENTED`. Na integração da feira, transcrição e descrição podem chegar prontas pela API escolar.
* A API escolar fornece `from_ref` pseudônimo, não o telefone. O histórico pode mostrar o nome de um professor vinculado ou um telefone que já exista no banco; não consegue descobrir o número de um aluno a partir do identificador anônimo.
* O histórico local registra a hora em que o SAE recebeu a mensagem. Embora a inbox forneça `ts`, esse horário original ainda não é salvo no banco. A tela exibe o registro local no fuso `America/Cuiaba`.
* A rota de resposta exige o ID de uma mensagem recebida. A API escolar atual não permite iniciar conversa com um número arbitrário nem enviar notificações em massa.
* Sem HTTPS embutido: use nginx/Caddy com TLS se o painel for publicado fora do computador local.
* Sessões ficam em memória e não sobrevivem a reinício do servidor.
* O hash atual é SHA-256 com salt; antes de produção, migrar para Argon2id ou bcrypt.
* O teste do servidor HTTP precisa de permissão para abrir sockets; em ambientes restritos, ele é pulado.

## 15. Próximas etapas

* Validar o fluxo completo de inbox, IA e resposta em um celular autorizado, usando as credenciais locais sem publicá-las.
* Definir com o mantenedor da API escolar se ela pode fornecer telefone real e horário original (`ts`); atualizar o SAE somente se o contrato for ampliado.
* Implementar provedores locais de transcrição e visão se o funcionamento sem os serviços da API escolar for necessário.
* Migrar hashes SHA-256 para Argon2id ou bcrypt.
* Persistir sessões em SQLite ou Redis.
* Executar os testes HTTP em um ambiente com sockets habilitados.
