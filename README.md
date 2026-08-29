# SAE — Sistema Auxiliar Escolar

Sistema de gestão de tarefas e comunicados para professores, alunos e coordenação.

## Como executar

1. No terminal MSYS2, entre no back-end e compile:

   ```bash
   cd "/c/SAE-Sistema_Auxiliar_Escolar/projeto de ciencias/school-system"
   make
   make install
   sae servidor
   ```

2. No navegador, abra `ScienceFair2026/index.html`.

3. Entre com um usuário já criado. No primeiro uso, com o servidor ligado, crie o coordenador pelo terminal:

   ```bash
   sae -ca -n "Nome" -p "+5566997213036" -s "sua-senha"
   ```

O front-end se comunica com `http://127.0.0.1:8080/api`. O token de login é salvo somente no navegador usado.

## Fluxo inicial

1. Faça login como coordenador.
2. Crie professor e turmas pelo CLI `sae`.
3. Vincule o professor à turma.
4. Faça login no navegador como professor e cadastre tarefas.

As tarefas e comunicados enviados pelas telas passam a ser persistidos no banco SQLite do servidor.
