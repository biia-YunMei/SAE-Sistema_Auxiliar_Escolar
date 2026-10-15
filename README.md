<div align="center">

# SAE — Sistema Auxiliar Escolar

### Organização escolar por tarefas, comunicados e WhatsApp

![C11](https://img.shields.io/badge/back--end-C11-A8B9CC?logo=c&logoColor=white)
![SQLite](https://img.shields.io/badge/banco-SQLite-003B57?logo=sqlite&logoColor=white)
![API](https://img.shields.io/badge/API-HTTP%2FJSON-0B7285)
![Status](https://img.shields.io/badge/status-em%20desenvolvimento-F59F00)

[Começar](#começar) · [Recursos](#recursos) · [Comandos](#comandos-principais) · [WhatsApp](#whatsapp-pela-api-escolar) · [Documentação](#documentação)

</div>

---

## Sobre o projeto

O SAE é um sistema para apoiar a comunicação entre coordenação, professores e alunos. O back-end é escrito em C11, expõe uma API HTTP/JSON e guarda os dados em SQLite.

A coordenação administra usuários, turmas e comunicados. Professores podem cadastrar e acompanhar tarefas. A IA pode usar Gemini 3.8 Flash com chave armazenada somente no `.env` local do back-end. Para a feira, a integração de mensagens usa a API escolar já fornecida; não é necessário criar aplicativo ou webhook na Meta.

## Recursos

| Área | Disponível atualmente |
| --- | --- |
| Usuários | Cadastro e listagem de coordenadores, professores e alunos. |
| Acesso | Login por telefone e senha; token de sessão mantido em memória pelo servidor. |
| Turmas | Criação de turmas e vínculo de professores e alunos. |
| Tarefas | Criação e listagem por turma; edição e exclusão pela API. |
| Comunicados | API para criar e consultar avisos. |
| WhatsApp | Com a integração configurada, o cliente consulta a inbox a cada 20 segundos, filtra a moderação e responde citando o ID recebido. A API fornece `from_ref` pseudônimo, não o telefone, e não permite iniciar conversa com um número arbitrário. |
| Histórico de mensagens | Exibe nome/telefone quando esses dados existem no SAE; pela API escolar, mensagens de alunos chegam apenas com identificador pseudônimo. O horário exibido é o registro local, no fuso de Cuiabá. |
| IA e mídia | A IA remota é opcional. A API escolar pode fornecer transcrição/descrição; os módulos locais de áudio e imagem ainda são stubs. |
| Segurança | Permissões por perfil, SQL parametrizado, validação de entrada e limite de tentativas de login. |
| Portabilidade | Build para MSYS2/Windows, Ubuntu e derivados, NixOS e outras distribuições Linux. |

## Estrutura

```text
SAE-Sistema_Auxiliar_Escolar/
├── ScienceFair2026/                 # Front-end (mantido separadamente)
├── projeto de ciencias/
│   └── school-system/               # Back-end C11
│       ├── include/                 # Interfaces públicas
│       ├── src/                     # Implementação da API e CLI
│       ├── testes/                  # Testes automatizados
│       ├── Makefile                 # Compilação e instalação do comando
│       ├── flake.nix                # Ambiente NixOS
│       └── docs/                    # Documentação técnica do back-end
└── README.md
```

## Começar

O servidor deve ficar aberto em um terminal. Os comandos administrativos são executados em outro terminal.

### Windows com MSYS2

Use o terminal **UCRT64**:

```bash
cd "/c/SAE-Sistema_Auxiliar_Escolar/projeto de ciencias/school-system"
make
sae servidor
```

O `make` compila e atualiza o comando `sae`. Caso seja a primeira instalação e o terminal não encontre o comando, execute uma vez:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### Ubuntu, Debian, Mint e Pop!_OS

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev libssl-dev

cd "projeto de ciencias/school-system"
make
sae servidor
```

### NixOS

```bash
cd "projeto de ciencias/school-system"
nix develop
make
sae servidor
```

## Primeiro acesso

Com o servidor em execução, abra outro terminal e crie o primeiro coordenador apenas uma vez:

```bash
sae -ca -n "Seu nome" -p "+5566999999999"
```

Depois, entre no sistema:

```bash
sae login -p "+5566999999999"
```

O terminal solicita a senha sem exibi-la. Não passe senhas como argumentos. Para encerrar o acesso neste computador:

```bash
sae logout
```

O logout apaga o token salvo localmente e invalida a sessão no servidor. Para usar comandos protegidos novamente, faça login de novo.

## Comandos principais

| Objetivo | Comando |
| --- | --- |
| Ver toda a ajuda | `sae --help` |
| Iniciar o servidor | `sae servidor` |
| Entrar | `sae login -p TELEFONE` (a senha é solicitada sem aparecer na tela) |
| Sair | `sae logout` |
| Criar usuário | `sae usuario criar -n NOME -p TELEFONE -t COORD\|PROF\|STUD` |
| Listar usuários | `sae usuario listar` |
| Criar turma | `sae turma criar -n NOME -g SERIE -t MANHA\|TARDE\|NOITE\|INTEGRAL` |
| Listar turmas | `sae turma listar` |
| Vincular professor | `sae turma vincular -c ID_TURMA -u ID_USUARIO_PROF` |
| Vincular aluno | `sae aluno vincular -u ID_ALUNO -c ID_TURMA` |
| Criar tarefa | `sae tarefa criar -c ID_TURMA -u ID_PROF -m MATERIA -n TITULO -v AAAA-MM-DD [-d DESCRICAO]` |
| Listar tarefas | `sae tarefa listar -c ID_TURMA` |

Exemplo de criação de professor, turma e tarefa:

```bash
sae usuario criar -n "Ana" -p "+5566999999999" -t PROF
sae turma criar -n "1º Ano A" -g "1º Ano" -t MANHA
sae turma vincular -c 1 -u 2
sae tarefa criar -c 1 -u 1 -m "Matemática" -n "Exercícios da página 20" -v 2026-10-15
```

> `ID_USUARIO_PROF` é o ID mostrado em `sae usuario listar`. Já `ID_PROF`, na criação de tarefa, é o ID interno do professor. Consulte a API ou o banco caso seja necessário identificá-lo.

## WhatsApp pela API escolar

Para a feira, o SAE deve consumir a API já hospedada em `https://api.cidades.io`. O programa consulta mensagens quando necessário; não precisa manter servidor público ou webhook, nem configurar credenciais da Meta.

### Validar acesso antes de programar

Use a chave `X-Escola-Key` que foi fornecida separadamente. Não a coloque neste README, no front-end, no Git ou em mensagens de suporte. O `.gitignore` da raiz já exclui arquivos `.env`.

No terminal, leia a chave sem exibi-la nem gravá-la no histórico e consulte o endpoint de identificação:

```bash
read -rsp "X-Escola-Key: " ESCOLA_KEY; echo
curl -sS -H "X-Escola-Key: $ESCOLA_KEY" \
  https://api.cidades.io/api/escola/sandbox/whoami
```

Se `whoami` responder com erro `chave_invalida`, pare e peça uma nova chave ao responsável. Não tente repetidamente. Depois, do celular que será usado no teste, envie o código de convite fornecido separadamente para `+55 66 99669-5016` e mande `oi`. Consulte a caixa de entrada:

```bash
curl -sS -H "X-Escola-Key: $ESCOLA_KEY" \
  "https://api.cidades.io/api/escola/sandbox/inbox?limit=5"
```

Para usar o cliente C integrado ao executável, copie `.env.example` para `.env` dentro de `projeto de ciencias/school-system`, preencha `ESCOLA_API_KEY` localmente e execute:

```bash
sae escola whoami
sae escola inbox -l 5
sae escola reply -i m_182 -m "Anotei sua mensagem."
sae escola ref
```

O cliente grava o cursor em `data/escola_api.cursor` por padrão. Para mudar o local, defina `ESCOLA_CURSOR_PATH`. O primeiro `inbox` sem cursor consulta as mensagens iniciais ainda disponíveis; chamadas seguintes usam o cursor salvo.

### Contrato da API

As rotas documentadas para este projeto são:

| Método e rota | Uso |
| --- | --- |
| `GET /api/escola/sandbox/whoami` | Confirma a chave e informa o modo de recebimento e o tamanho da allowlist. |
| `GET /api/escola/sandbox/inbox?limit=5` | Busca mensagens disponíveis. |
| `GET /api/escola/sandbox/inbox?since=CURSOR` | Busca mensagens seguintes sem consumir nem apagar as anteriores. |
| `POST /api/escola/sandbox/reply` | Responde citando o ID da mensagem com `in_reply_to`. |
| `/api/escola/sandbox/ref` | Referência da API. |

Exemplo de resposta a uma mensagem recebida:

```bash
curl -X POST \
  -H "X-Escola-Key: $ESCOLA_KEY" \
  -H "Content-Type: application/json" \
  -d '{"in_reply_to":"m_182","text":"Anotei sua mensagem."}' \
  https://api.cidades.io/api/escola/sandbox/reply
```

Quando terminar os testes no terminal, apague a chave da variável da sessão:

```bash
unset ESCOLA_KEY
```

O campo `in_reply_to` deve conter o `id` retornado pela inbox. Não há rota para enviar mensagem a um telefone arbitrário. A resposta é enviada exatamente como escrita; revise qualquer texto gerado por IA antes de publicá-lo.

### Recebimento, privacidade e cursor

- Guarde `proximo_cursor` em arquivo persistente e envie-o como `since` na próxima consulta. Repetir um cursor devolve as mesmas mensagens.
- Use `from_ref` como identificador pseudônimo; a API não fornece o telefone do remetente.
- Áudios chegam transcritos no campo `text`. Imagens chegam descritas em dados estruturados, não como arquivo bruto.
- Só exiba mensagens com `moderacao: "ok"`. Trate `bloqueada` e `indisponivel` como conteúdo que não deve ser exibido.
- Respeite `PARAR`, `SAIR` e `VOLTAR`, os limites de chamadas e `Retry-After`. Não faça tentativas em laço apertado.
- Em erros, programe pelo campo estável `erro`; `mensagem` é texto para humanos. `fora_da_janela_24h` exige que a pessoa mande uma nova mensagem.

As rotas antigas de WhatsApp Business Cloud, tokens da Meta e webhook não fazem parte deste fluxo de feira. Não use `WHATSAPP_API_TOKEN`, `WHATSAPP_PHONE_NUMBER_ID`, `WHATSAPP_APP_SECRET` ou `WHATSAPP_VERIFY_TOKEN` para integrar a API escolar.

## Desenvolvimento e qualidade

```bash
make            # Compila, executa os testes e atualiza o comando sae
make tests      # Compila e executa os testes
make debug      # Compila com símbolos de depuração
make sanitize   # Compila com AddressSanitizer e UBSan
make clean      # Remove arquivos gerados
```

## Documentação

- [Uso e permissões](projeto%20de%20ciencias/school-system/docs/USO_E_PERMISSOES.md)
- [Configuração e portabilidade](projeto%20de%20ciencias/school-system/docs/CONFIGURACAO_E_PORTABILIDADE.md)
- Referência atualizada da API: `GET /api/escola/sandbox/ref` (base `https://api.cidades.io`).
- [Documentação detalhada do back-end](projeto%20de%20ciencias/school-system/README.md)

## Próximos passos

- [x] Integrar no back-end C as rotas `whoami`, `inbox`, `reply` e `ref`, com cursor persistente e filtro de moderação.
- [ ] Validar os comandos do cliente C com a chave local e testar uma resposta no celular autorizado.
- [ ] Persistir o `ts` original da API; atualmente o histórico registra quando o SAE recebeu a mensagem e exibe no fuso de Cuiabá.
- [ ] Decidir, com quem mantém a API escolar, se o telefone real poderá ser fornecido. Sem esse dado, o SAE só pode exibir `from_ref` e nomes de contatos já vinculados.
- [ ] Implementar provedores locais de áudio e imagem; `src/transcription.c` e `src/image.c` ainda retornam `ERROR_NOT_IMPLEMENTED`.
- [ ] Adicionar suporte de envio a número arbitrário somente se a API escolar oferecer essa rota. Hoje o envio é resposta a uma mensagem recebida.
- [ ] Usar hash de senha com Argon2 ou bcrypt antes de produção.
- [ ] Persistir sessões em banco de dados ou Redis.
- [ ] Executar os testes HTTP em ambiente com permissão para abrir sockets; no ambiente de desenvolvimento usado, esse teste foi pulado por essa restrição.

---

<div align="center">
  Feito para aprender, organizar e aproximar a escola.
</div>
