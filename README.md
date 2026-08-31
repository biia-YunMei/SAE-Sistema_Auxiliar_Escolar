<div align="center">

# SAE

### Sistema Auxiliar Escolar

Gestão de tarefas, turmas e comunicados para professores, alunos e coordenação.

![C11](https://img.shields.io/badge/back--end-C11-A8B9CC?logo=c&logoColor=white)
![SQLite](https://img.shields.io/badge/banco-SQLite-003B57?logo=sqlite&logoColor=white)
![HTTP](https://img.shields.io/badge/API-HTTP%2FJSON-0B7285)
![Status](https://img.shields.io/badge/status-em%20desenvolvimento-F59F00)

[Começar](#-começar) · [Recursos](#-recursos) · [Arquitetura](#-arquitetura) · [Documentação](#-documentação)

</div>

---

## ✨ Sobre o projeto

O **SAE — Sistema Auxiliar Escolar** é um projeto educacional desenvolvido para centralizar a comunicação e a organização escolar. O sistema possui uma API em C11, persistência com SQLite, interface web estática e uma CLI chamada `sae` para administração pelo terminal.

> O projeto foi pensado para uma feira de ciências, mas segue práticas reais como autenticação por token, validação de entradas, SQL parametrizado e migrations de banco.

## 🚀 Recursos

| Área | O que o SAE oferece |
| --- | --- |
| 👤 Usuários | Cadastro de coordenadores, professores e alunos. |
| 🏫 Turmas | Criação de turmas e vínculo de professores. |
| 📝 Tarefas | Cadastro, listagem, edição e remoção de tarefas. |
| 📢 Comunicados | Registro de comunicados por turma. |
| 🔐 Segurança | Login por telefone, sessão com token e controle de permissões. |
| 💾 Banco | SQLite com migrations, chaves estrangeiras e WAL. |
| 💬 Integrações | Base para WhatsApp, IA, áudio e imagem. |

## 🧱 Arquitetura

```text
┌───────────────────────────────┐
│        Interface Web           │
│       ScienceFair2026/         │
└───────────────┬───────────────┘
                │ HTTP / JSON
┌───────────────▼───────────────┐
│         API SAE em C11         │
│  rotas → controllers → domínio │
└───────────────┬───────────────┘
                │ SQL preparado
┌───────────────▼───────────────┐
│            SQLite              │
│       usuários • turmas         │
│   tarefas • comunicados         │
└───────────────────────────────┘
```

## 📁 Estrutura

```text
SAE-Sistema_Auxiliar_Escolar/
├── ScienceFair2026/              # Interface web importada
├── projeto de ciencias/
│   └── school-system/            # Back-end C11
│       ├── include/              # Interfaces públicas
│       ├── src/                  # Implementação
│       ├── testes/               # Testes automatizados
│       ├── Makefile              # Build e comandos
│       └── flake.nix             # Ambiente NixOS
├── docs/                         # Documentação técnica
└── README.md
```

## 🏁 Começar

### Windows com MSYS2

No terminal **UCRT64** do MSYS2:

```bash
cd "/c/SAE-Sistema_Auxiliar_Escolar/projeto de ciencias/school-system"
make
make install
sae servidor
```

Em outro terminal, crie o primeiro coordenador:

```bash
sae -ca -n "Seu nome" -p "+5566999999999" -s "sua-senha"
```

### Ubuntu, Debian, Mint e Pop!_OS

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libsqlite3-dev libcurl4-openssl-dev libssl-dev

cd "projeto de ciencias/school-system"
make
./build/sae servidor
```

### NixOS

```bash
cd "projeto de ciencias/school-system"
nix develop
make
./build/sae servidor
```

## 💻 Comandos principais

```bash
sae --help
sae servidor
sae login -p "+5566999999999" -s "sua-senha"
sae usuario listar
sae turma listar
sae tarefa listar -c 1
```

## 🧪 Qualidade

```bash
make tests       # Executa a suíte de testes
make sanitize    # Compila com AddressSanitizer e UBSan
make debug       # Compila com símbolos de depuração
```

## 📚 Documentação

- [Arquitetura do back-end](docs/ARQUITETURA.md)
- [Referência da API HTTP](docs/API.md)
- [Padrões de código e contribuição](docs/CONTRIBUINDO.md)
- [Documentação detalhada do back-end](projeto%20de%20ciencias/school-system/README.md)

## 🗺️ Próximos passos

- [ ] Painel web com fluxo completo de coordenação.
- [ ] Persistência de sessões em Redis ou SQLite.
- [ ] Integração completa com WhatsApp Business.
- [ ] Melhorias de acessibilidade e responsividade.
- [ ] Hash de senha com Argon2 ou bcrypt para produção.

---

<div align="center">
  Feito para aprender, organizar e aproximar a escola. 🎓
</div>
