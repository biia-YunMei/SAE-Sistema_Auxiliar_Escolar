# Manual: tarefas e recados de professores pelo WhatsApp

Este manual explica o fluxo local do SAE para vincular o WhatsApp de um professor e administrar tarefas e lembretes por mensagem.

## Como o fluxo funciona

1. O professor gera no painel um código temporário de vínculo.
2. Envia `VINCULAR <código>` pelo WhatsApp usado para falar com o SAE.
3. O SAE associa o remetente recebido da API ao cadastro autenticado do professor.
4. O professor envia uma tarefa ou um recado. O SAE valida os dados, a turma e as permissões.
5. O SAE mostra uma prévia. A publicação só acontece depois de o professor responder `SIM`.

O remetente da API é pseudônimo. Por isso, faça o vínculo a partir do WhatsApp que o professor pretende usar para conversar com o SAE. O código não deve ser compartilhado.

## Antes de começar

- O professor precisa ter uma conta ativa no SAE e estar cadastrado como professor.
- A coordenação precisa associar o professor às turmas que ele administra.
- O servidor SAE e o robô que consulta a inbox precisam estar ativos e configurados para receber e responder mensagens.
- A página deve ser aberta pelo servidor SAE, por exemplo `http://127.0.0.1:8080/`, e não como arquivo `file://`.
- O cartão de vínculo aparece somente no servidor local. Ele fica oculto ao abrir o HTML diretamente ou no site `escola.cidades.io`.

## Vincular o WhatsApp

1. Entre no painel com a conta de professor ou faça login pelo terminal (`sae login -p TELEFONE`).
2. No painel, abra **Visão geral** e clique em **Vincular meu WhatsApp**. Pelo terminal, execute `sae whatsapp vincular`.
3. O SAE mostrará um código e o prazo para usá-lo. O código é válido por 10 minutos e só pode ser usado uma vez.
4. Pelo WhatsApp que será vinculado, envie para **(66) 99669-5016**:

   ```text
   VINCULAR <código>
   ```

5. Aguarde a confirmação **“WhatsApp vinculado ao seu cadastro de professor”**.

Se aparecer “Código inválido, expirado ou já utilizado”, gere outro código no painel e envie-o dentro do prazo. O código anterior deixa de valer quando um novo é gerado.

## Desvincular o WhatsApp

O professor pode clicar em **Desvincular WhatsApp** no cartão da aba **Visão geral** ou executar `sae whatsapp desvincular` enquanto estiver logado com a própria conta. A operação remove a associação e descarta rascunhos ainda não confirmados. Tarefas e avisos já publicados permanecem no sistema. Para vincular novamente, gere outro código.

## Cadastrar uma tarefa

Envie uma mensagem com turma, matéria, conteúdo e prazo. Com a IA configurada, pode escrever naturalmente, por exemplo:

```text
Tarefa para o 9º A, Matemática: fazer as questões 1 a 5 da página 32. Entrega em 20/10/2026.
```

Também é possível enviar áudio quando o serviço de mensagens disponibilizar a transcrição em texto para o SAE. O processamento da tarefa usa esse texto transcrito.

O SAE prepara um resumo com os dados reconhecidos. Confira a turma, a matéria, o conteúdo e a data antes de confirmar:

- `SIM` ou `CONFIRMAR`: grava a tarefa no banco local `school.db`.
- `CANCELAR`: descarta a publicação pendente.
- `EDITAR`: descarta o rascunho para que você envie os dados corrigidos em uma nova mensagem.

Para reduzir ambiguidades, prefira informar a data completa no formato `AAAA-MM-DD`, por exemplo `2026-10-20`. A turma deve existir e estar ativa, e o professor precisa estar vinculado a ela. Uma mensagem incompleta ou uma turma sem vínculo não é publicada.

Se a IA não conseguir interpretar a mensagem, envie os dados neste formato:

```text
turma: 9º A | matéria: Matemática | título: Questões da página 32 | descrição: Fazer as questões 1 a 5 | entrega: 2026-10-20
```

Mesmo quando a IA interpreta a mensagem, é o SAE que valida a data, verifica a turma e autoriza o professor. A IA não grava a tarefa diretamente.

## Consultar e editar tarefas

Envie:

```text
MINHAS TAREFAS
```

A resposta mostra as tarefas ativas e seus IDs. Para editar, informe o ID e ao menos um campo:

```text
EDITAR TAREFA 42 | título: Lista de exercícios | descrição: Fazer as questões 1 a 8 | entrega: 2026-10-21
```

Campos omitidos permanecem iguais. A turma não pode ser trocada pelo WhatsApp. O SAE apresenta uma prévia; responda `SIM` para salvar ou `CANCELAR` para descartar a edição. Um professor só pode editar as próprias tarefas e precisa continuar autorizado para a turma.

## Publicar e editar lembretes ou recados

Para publicar um lembrete para uma turma, use o nome cadastrado da turma:

```text
LEMBRETE 9º A: trazer o livro de Matemática na próxima aula.
```

O SAE prepara uma prévia. Responda `SIM` para publicar ou `CANCELAR` para descartar. Também são aceitos os prefixos `RECADO`, `AVISO` e `COMUNICADO`, no mesmo formato. Prefira o nome oficial da turma que aparece no painel.

Para consultar os lembretes publicados pelo professor:

```text
LEMBRETES
```

A resposta mostra os IDs. Para editar o texto de um deles:

```text
EDITAR LEMBRETE 17: trazer o livro e o caderno de Matemática.
```

Confirme a prévia com `SIM` ou descarte com `CANCELAR`. Só é possível editar um lembrete próprio associado a uma turma ativa à qual o professor ainda tenha acesso.

**O lembrete fica salvo para a turma; ele não dispara uma mensagem individual automaticamente para cada aluno.** Os alunos podem consultar os avisos da própria turma pelo WhatsApp.

## Ajuda e publicações pendentes

Envie `AJUDA` ou `MENU` para receber os comandos principais. O SAE permite resolver uma publicação pendente por vez. Se já houver uma prévia aguardando confirmação, responda `SIM` ou `CANCELAR` antes de iniciar outra publicação ou edição.

## Segurança e permissões

- O código de vínculo expira em 10 minutos, é de uso único e o banco guarda somente seu hash.
- O servidor identifica o professor vinculado e verifica sua conta ativa.
- Tarefas e lembretes só podem ser publicados em turmas ativas às quais o professor esteja associado.
- Edição de tarefa ou lembrete verifica autoria e autorização no back-end; ocultar opções no painel não substitui essas verificações.
- A confirmação é obrigatória antes de gravar novas tarefas, recados ou alterações.

## Reserva online e SAE local

O modo de atendimento das mensagens de professores é controlado pela configuração do serviço online. Até que a equipe responsável altere essa configuração, a reserva online pode continuar atendendo o professor e gravando a tarefa somente na base online. Nesse modo, ela não aparece automaticamente no `school.db` local.

Depois de testar o vínculo local com sucesso, a equipe responsável pela reserva pode direcionar as mensagens de professores ao SAE local. Quando o SAE local recebe e processa a publicação confirmada, a tarefa entra no `school.db` e aparece nas consultas locais. Não altere configurações do serviço online por este manual.

Para cadastrar um professor novo durante a feira, siga também a orientação da equipe e cadastre-o na plataforma online, com o telefone, quando isso for solicitado.

## Problemas comuns

| Situação | O que fazer |
|---|---|
| O botão não gera o código | Entre novamente com uma conta de professor ativa e confirme que o servidor SAE está acessível. |
| Código inválido ou expirado | Gere um novo código e envie `VINCULAR <código>` em até 10 minutos. |
| “Você não tem acesso a essa turma” | Peça à coordenação para associar o professor à turma no SAE. |
| O SAE pede mais dados ou não entende a tarefa | Informe turma, matéria, título/conteúdo e data completa; use o formato estruturado deste manual. |
| A tarefa foi confirmada, mas não aparece no painel local | Confira se a mensagem foi direcionada ao SAE local e se o robô da inbox está ativo. A reserva online e o `school.db` local são bases separadas. |
| O WhatsApp não responde | Verifique se o servidor e o robô estão ativos e peça à equipe responsável para conferir o modo de encaminhamento da reserva. |
