<div align="center">

# SAE — Sistema Auxiliar Escolar

### Organização escolar por tarefas, comunicados e WhatsApp

![C11](https://img.shields.io/badge/back--end-C11-A8B9CC?logo=c&logoColor=white)
![SQLite](https://img.shields.io/badge/banco-SQLite-003B57?logo=sqlite&logoColor=white)
![API](https://img.shields.io/badge/API-HTTP%2FJSON-0B7285)
![Status](https://img.shields.io/badge/status-em%20desenvolvimento-F59F00)

[Começar](#começar) · [Recursos](#recursos) · [Comandos](#comandos-principais) · [WhatsApp](#whatsapp) · [Documentação](#documentação)

</div>

---

## Sobre o projeto

O SAE é um sistema para apoiar a comunicação entre coordenação, professores e alunos. O back-end é escrito em C11, expõe uma API HTTP/JSON e guarda os dados em SQLite.

A coordenação administra usuários, turmas e comunicados. Professores podem cadastrar e acompanhar tarefas. Alunos recebem informações da turma e podem consultá-las pelo WhatsApp quando a integração estiver configurada.

## Recursos

| Área | Disponível atualmente |
| --- | --- |
| Usuários | Cadastro e listagem de coordenadores, professores e alunos. |
| Acesso | Login por telefone e senha; sessão por token e logout. |
| Turmas | Criação de turmas e vínculo de professores e alunos. |
| Tarefas | Criação e listagem por turma; edição e exclusão pela API. |
| Comunicados | API para criar e consultar avisos. |
| WhatsApp | Webhook, envio individual, envio para turma e fluxo de tarefas por mensagem. |
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
│       └── flake.nix                # Ambiente NixOS
├── docs/                            # Documentação técnica
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
sae -ca -n "Seu nome" -p "+5566999999999" -s "uma-senha-segura"
```

Depois, entre no sistema:

```bash
sae login -p "+5566999999999" -s "uma-senha-segura"
```

Para encerrar o acesso neste computador:

```bash
sae logout
```

O logout apaga o token salvo localmente e invalida a sessão no servidor. Para usar comandos protegidos novamente, faça login de novo.

## Comandos principais

| Objetivo | Comando |
| --- | --- |
| Ver toda a ajuda | `sae --help` |
| Iniciar o servidor | `sae servidor` |
| Entrar | `sae login -p TELEFONE -s SENHA` |
| Sair | `sae logout` |
| Criar usuário | `sae usuario criar -n NOME -p TELEFONE -t COORD\|PROF\|STUD -s SENHA` |
| Listar usuários | `sae usuario listar` |
| Criar turma | `sae turma criar -n NOME -g SERIE -t MANHA\|TARDE\|NOITE\|INTEGRAL` |
| Listar turmas | `sae turma listar` |
| Vincular professor | `sae turma vincular -c ID_TURMA -u ID_USUARIO_PROF` |
| Vincular aluno | `sae aluno vincular -u ID_ALUNO -c ID_TURMA` |
| Criar tarefa | `sae tarefa criar -c ID_TURMA -u ID_PROF -m MATERIA -n TITULO -v AAAA-MM-DD [-d DESCRICAO]` |
| Listar tarefas | `sae tarefa listar -c ID_TURMA` |

Exemplo de criação de professor, turma e tarefa:

```bash
sae usuario criar -n "Ana" -p "+5566999999999" -t PROF -s "senha-da-ana"
sae turma criar -n "1º Ano A" -g "1º Ano" -t MANHA
sae turma vincular -c 1 -u 2
sae tarefa criar -c 1 -u 1 -m "Matemática" -n "Exercícios da página 20" -v 2026-10-15
```

> `ID_USUARIO_PROF` é o ID mostrado em `sae usuario listar`. Já `ID_PROF`, na criação de tarefa, é o ID interno do professor. Consulte a API ou o banco caso seja necessário identificá-lo.

## WhatsApp

Com a integração configurada, a coordenação pode enviar uma mensagem para uma pessoa ou para todos os alunos de uma turma:

```bash
sae whatsapp enviar -p "+5566999999999" -m "Reunião amanhã às 14h."
sae whatsapp turma -c 1 -m "A prova de Ciências será na sexta-feira."
```

Para ativar envio real pelo WhatsApp Business Cloud, copie `.env.example` para `.env` dentro de `projeto de ciencias/school-system` e informe as credenciais da Meta. Depois, execute `make` novamente e deixe `sae servidor` em execução.

Mais detalhes: [Integração com WhatsApp Business Cloud](docs/WHATSAPP.md).

## Desenvolvimento e qualidade

```bash
make            # Compila e atualiza o comando sae
make tests      # Compila e executa os testes
make debug      # Compila com símbolos de depuração
make sanitize   # Compila com AddressSanitizer e UBSan
make clean      # Remove arquivos gerados
```

## Documentação

- [Arquitetura do back-end](docs/ARQUITETURA.md)
- [Referência da API HTTP](docs/API.md)
- [Padrões de código e contribuição](docs/CONTRIBUINDO.md)
- [Integração com WhatsApp Business Cloud](docs/WHATSAPP.md)
- [Documentação detalhada do back-end](projeto%20de%20ciencias/school-system/README.md)

## Próximos passos

- [ ] Confirmar tarefas e comunicados diretamente pelo WhatsApp.
- [ ] Processar áudio e imagem para apoiar o atendimento aos alunos.
- [ ] Usar hash de senha com Argon2 ou bcrypt antes de produção.
- [ ] Persistir sessões em banco de dados ou Redis.

---

<div align="center">
  Feito para aprender, organizar e aproximar a escola.
</div>
