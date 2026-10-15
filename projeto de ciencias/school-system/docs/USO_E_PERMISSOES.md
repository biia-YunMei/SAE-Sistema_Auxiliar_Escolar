# Uso, comandos e permissões

Para o passo a passo completo de vínculo, tarefas, edições e lembretes, consulte o
[Manual de WhatsApp para professores](MANUAL_WHATSAPP_PROFESSORES.md).

## Primeiro acesso

Inicie a API em um terminal:

```bash
sae servidor
```

Em outro terminal, crie o primeiro coordenador e entre no sistema:

```bash
sae -ca -n "Nome do coordenador" -p +5566999999999
sae login -p +5566999999999
```

O terminal solicita a senha sem exibi-la. Não informe a senha como argumento,
pois ela pode ficar no histórico do shell e visível na lista de processos.

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
sae usuario criar -n NOME -p TELEFONE -t COORD|PROF
sae usuario listar
sae usuario editar -u ID -n NOME -p TELEFONE [--senha]
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

Na página `professores.html`, entre em **Visão geral** e selecione
**Vincular meu WhatsApp**. Envie `VINCULAR <código>` pelo WhatsApp do SAE em até
10 minutos. Depois do vínculo, o professor pode enviar uma tarefa em linguagem
natural. O sistema mostra uma prévia e só publica após `SIM`.
Para remover o vínculo da própria conta, use **Desvincular WhatsApp** no painel
ou `sae whatsapp desvincular`. Rascunhos não confirmados são descartados;
tarefas e avisos já publicados permanecem.
`MINHAS TAREFAS` lista as tarefas próprias; para editar, envie
`EDITAR TAREFA <id> | título: novo título | matéria: matéria | descrição: texto | entrega: AAAA-MM-DD`.
Campos não informados permanecem iguais e a turma não pode ser trocada pelo
WhatsApp. Para lembretes por turma, use `LEMBRETE <turma>: <texto>`;
`LEMBRETES` lista os ativos e `EDITAR LEMBRETE <id>: <novo texto>` prepara
uma edição. Confirme publicações e alterações com `SIM`; use `CANCELAR` para
descartar. O professor só pode alterar tarefas e lembretes próprios, em turmas
ativas às quais continua vinculado.

O parâmetro `-u ID_PROF` é necessário ao coordenador criar uma tarefa. Para o
professor, o sistema identifica automaticamente o próprio perfil.

## Saída do terminal

O SAE usa molduras Unicode em terminais interativos UTF-8 e texto simples em
terminais incompatíveis ou em saída redirecionada.

```bash
SAE_UI=unicode sae usuario listar  # força a saída com molduras
SAE_UI=plain sae usuario listar    # força a saída simples
```
