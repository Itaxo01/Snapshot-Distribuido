# Snapshot Distribuído (Chandy–Lamport) — Cluster de Computação com *Work-Stealing*

Trabalho 2 de **INE5418 — Computação Distribuída** (UFSC).
Implementação de uma aplicação distribuída funcional cujo *building block* central é o
**Snapshot Distribuído**, capturado pelo algoritmo de **Chandy–Lamport**. Múltiplos
processos independentes comunicam-se por **Berkeley Sockets** (TCP e UDP).

---

## 1. Visão geral

A aplicação simula um **cluster heterogêneo** que processa tarefas. Os workers iniciam
**vazios** (0 tarefas) e a carga é **injetada em runtime** pelo Monitor — você escolhe o
nó e quantas tarefas enviar, quando quiser. Os nós (*workers*) fazem **balanceamento de
carga por *work-stealing***: transferem lotes de tarefas entre si dinamicamente, sem um
coordenador central.

Por causa dessa comunicação constante, tentar observar "quantas tarefas existem no
sistema" somando o estado de cada nó **quase nunca dá o valor correto** — sempre há
lotes *em trânsito* na rede, invisíveis para qualquer nó isolado.

O **Snapshot Distribuído** resolve isso: ele captura um **estado global consistente**
(estado local de cada processo **+** as mensagens em trânsito nos canais) sem parar o
sistema. Com ele demonstramos duas aplicações:

- **Conservação:** a soma de tarefas (`pendentes + concluídas + em trânsito`) é sempre
  igual ao total **injetado**. Uma leitura ingênua erra; o snapshot acerta.
- **Detecção de término:** o sistema só terminou quando **nenhum** nó tem tarefas
  pendentes **e nenhum** canal tem tarefas em trânsito — algo que a leitura ingênua
  (todos "ociosos") pode declarar cedo demais.

---

## 2. Arquitetura

Três executáveis independentes:

| Executável | Papel |
|---|---|
| **`orquestrador`** | *Entry point* único. Monta a topologia (grafo fortemente conexo), atribui portas/identidades, distribui a carga e lança (via `fork`/`exec`) um processo `entidade` por nó. Encaminha o sinal de encerramento aos filhos. |
| **`entidade`** (worker) | Nó do cluster. Processa tarefas, faz *work-stealing* com os vizinhos e participa do snapshot. Cada worker é um **processo separado** do SO. |
| **`monitor`** | Interface gráfica (Dear ImGui). Visualiza o estado em tempo real, dispara snapshots e exibe os resultados. |

**Comunicação (Berkeley Sockets):**
- **TCP peer-to-peer** entre workers: tarefas e **marcadores** do Chandy–Lamport (na
  mesma conexão, preservando a ordem FIFO exigida pelo algoritmo).
- **TCP de controle** Monitor → worker: comandos de iniciar snapshot e de injetar tarefas.
- **UDP** worker → Monitor: telemetria *fire-and-forget* (não bloqueia a rede de negócio).

As peças de snapshot são publicadas em arquivo (`snapshots/<id>/<no>.json`) e agregadas
pelo Monitor.

---

## 3. Dependências

- Compilador C++ com suporte a **C++17** (g++ ou clang)
- **CMake ≥ 3.16**
- **GLFW 3** e **OpenGL** (apenas para o `monitor`)
- **pkg-config**
- O `monitor` precisa de um **display gráfico** (X11/Wayland).

No Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libglfw3-dev libgl1-mesa-dev
```

A biblioteca Dear ImGui já está incluída no repositório (pasta `imgui/`).

---

## 4. Compilação

```bash
cmake -S . -B build
cmake --build build -j
```

Os binários ficam em `build/bin/`: `orquestrador`, `entidade` e `monitor`.

---

## 5. Execução

### Forma recomendada — script `run.sh`

Compila (se necessário), sobe o cluster e abre o Monitor. Fechar a janela do Monitor
(ou `Ctrl-C` no terminal) encerra **todos** os processos.

```bash
./run.sh
```

Parâmetros do cenário podem ser passados por variáveis de ambiente:

```bash
WORKERS=6 TOPOLOGIA=mesh ./run.sh
```

- `WORKERS` — número de workers (use **≥ 3**)
- `TOPOLOGIA` — `mesh` (todos conectados) ou `ring` (anel)

As tarefas **não** são parâmetro de inicialização — injete-as pelo Monitor em runtime
(ver seção 6).

### Forma manual (dois terminais)

Rode os dois **no mesmo diretório** (eles compartilham `cluster.json` e `snapshots/`):

```bash
# Terminal 1 — sobe o cluster e escreve cluster.json
./build/bin/orquestrador --workers 6 --topologia mesh

# Terminal 2 — abre o Monitor (lê cluster.json)
./build/bin/monitor cluster.json
```

`Ctrl-C` no orquestrador encerra todos os workers.

#### Opções do orquestrador

```
--workers N            (obrigatório) número de workers
--topologia ring|mesh  topologia de comunicação (padrão: ring)
--porta-base 7001      porta TCP de negócio do 1º worker (= identidade)
--controle-base 8001   porta TCP de controle do 1º worker
--telemetria 9000      porta UDP de telemetria do Monitor
--host 127.0.0.1       host de bind/roteamento
--entidade <caminho>   caminho do binário entidade (padrão: mesmo diretório)
```

Os workers iniciam com **0 tarefas**; a carga é injetada em runtime pelo Monitor.

O `entidade` **não** é executado à mão — o orquestrador o lança com os argumentos corretos.

---

## 6. Usando o Monitor (o cenário da demonstração)

Na janela do Monitor:

- **Injetar carga (runtime):** escolha um worker, um número de tarefas e clique em
  **`Injetar`**. O total do cluster cresce de acordo, e as barras daquele nó sobem. É a
  forma de "alimentar" o sistema — os workers começam vazios.
- **Barras por worker:** tarefas pendentes/concluídas e o fator de processamento (`xN`).
- **Soma global (UDP):** a leitura ingênua do sistema. Repare que ela quase sempre fica
  **abaixo** do total — o déficit são as tarefas em trânsito.
- **Botão `Pausar (caos)`:** congela a leitura UDP. A soma exibida fica (quase sempre)
  **inconsistente** — é a "foto" superficial de quem tenta olhar todos os nós ao mesmo tempo.
- **Botão `Capturar Snapshot (ordem)` + seletor `iniciador`:** dispara o algoritmo de
  Chandy–Lamport a partir do nó escolhido. O resultado aparece na lista e mostra a soma
  **exata** (`== total`, **CONSISTENTE**), além do status de **término**.
- **Listas `Sessão` / `Anteriores`:** snapshots desta execução vs. snapshots de execuções
  anteriores encontrados na pasta. O disparo é *não bloqueante* — vários snapshots podem
  rodar ao mesmo tempo.

**Roteiro sugerido:** **injete** uma carga grande em um nó (ex.: 100000 no worker 7001) e
deixe assentar um instante; clique em **Pausar** e observe a soma incorreta; depois clique
em **Capturar Snapshot** e observe a soma exata (`== total`). Espere o lote drenar e
capture de novo para ver o status mudar para **TERMINADO**.

> **Nota:** injete a carga e deixe-a assentar **antes** de capturar. A injeção viaja pelo
> canal de controle (fora do grafo capturado pelo snapshot); capturar no exato instante de
> uma injeção pode mostrar uma inconsistência transitória. Ver `Limitacoes.md`.

---

## 7. Saída (snapshots em JSON)

Cada snapshot gera, na pasta `snapshots/<id>/`:

- `<no>.json` — peça local de cada worker (estado + tarefas em trânsito por canal),
  escrita atomicamente pelo próprio worker;
- `final_snapshot.json` — agregação global escrita pelo Monitor (soma, consistência,
  término e o estado de cada nó).

O `<id>` é o identificador do snapshot (codifica o nó iniciador).

---

## 8. Configuração e ajustes

- **Forma do cluster** (quantos workers, tarefas, topologia): CLI do `orquestrador` /
  variáveis do `run.sh` (ver seção 5).
- **Comportamento e ritmo** (atraso simulado de rede, velocidade de processamento,
  política de *work-stealing*, escala da interface): constantes centralizadas em
  [`src/comum/parametros.hpp`](src/comum/parametros.hpp). Após alterar, recompile.

## Links para o Github e vídeo gravado
https://github.com/Itaxo01/Snapshot-Distribuido
https://youtu.be/C8WlWTu4JAY
