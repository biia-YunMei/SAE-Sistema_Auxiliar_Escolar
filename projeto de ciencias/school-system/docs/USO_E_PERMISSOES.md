# Uso, comandos e permissões

## Primeiro acesso

Inicie a API em um terminal:

```bash
sae servidor
```

Em outro terminal, crie o primeiro coordenador e entre no sistema:

```bash
sae -ca -n "Nome do coordenador" -p +5566999999999 -s sua_senha
sae login -p +5566999999999 -s sua_senha
```

O primeiro cadastro é público apenas enquanto não há coordenador. Depois disso,
as operações exigem uma sessão válida. A sessão local fica em `.sae-session` e
é removida por `sae logout`.

## Papéis

| Papel | Acesso |
|---|---|
| `COORD` | Todos os comandos. Pode cadastrar, editar e excluir usuários. |
| `PROF` | Lista usuários; cria e administra somente as próprias turmas e tarefas; vincula alunos às suas turmas; envia mensagens por WhatsApp. |
| `STUD` | Não possui login. Os dados de nome, telefone e série são cadastrados pela coordenação; consultas são feitas pelo WhatsApp sem autenticação. |

O cadastro de alunos é feito na aba **Alunos** da área da coordenação e não
cria senha ou sessão. O cadastro geral de usuários aceita somente coordenadores
e professores.

Todos os papéis podem usar `sae logout` e `sae --help`.

## Comandos

```bash
# Usuários (COORD)
sae usuario criar -n NOME -p TELEFONE -t COORD|PROF [-s SENHA]
sae usuario listar
sae usuario editar -u ID -n NOME -p TELEFONE [-s SENHA]
sae usuario excluir -u ID

# Turmas (COORD e PROF)
sae turma criar -n NOME -g SERIE [-t MANHA|TARDE|NOITE|INTEGRAL]
sae turma listar
sae turma editar -i ID -n NOME -g SERIE -t MANHA|TARDE|NOITE|INTEGRAL
sae turma excluir -i ID
sae turma vincular -c ID_TURMA -u ID_USUARIO_PROF  # COORD

# Alunos (COORD ou PROF vinculado)
sae aluno vincular -u ID_ALUNO -c ID_TURMA

# Tarefas (COORD e PROF)
sae tarefa criar -c ID_TURMA -m MATERIA -n TITULO -v AAAA-MM-DD [-d DESCRICAO]
sae tarefa criar -c ID_TURMA -u ID_PROF -m MATERIA -n TITULO -v AAAA-MM-DD
sae tarefa listar -c ID_TURMA
sae tarefa editar -i ID -m MATERIA -n TITULO -v AAAA-MM-DD [-d DESCRICAO]
sae tarefa excluir -i ID

# WhatsApp (COORD e PROF)
sae whatsapp enviar -p TELEFONE -m MENSAGEM
sae whatsapp turma -c ID_TURMA -m MENSAGEM
```

O parâmetro `-u ID_PROF` é necessário ao coordenador criar uma tarefa. Para o
professor, o sistema identifica automaticamente o próprio perfil.

## Saída do terminal

O SAE usa molduras Unicode em terminais interativos UTF-8 e texto simples em
terminais incompatíveis ou em saída redirecionada.

```bash
SAE_UI=unicode sae usuario listar  # força a saída com molduras
SAE_UI=plain sae usuario listar    # força a saída simples
```
