# Snapshot Distribuído (Chandy–Lamport) — Cluster de Computação com *Work-Stealing*

Trabalho 2 de **INE5418 — Computação Distribuída** (UFSC).
Implementação de uma aplicação distribuída funcional cujo *building block* central é o
**Snapshot Distribuído**, capturado pelo algoritmo de **Chandy–Lamport**. Múltiplos
processos independentes comunicam-se por **Berkeley Sockets** (TCP e UDP).

---

## 1. Visão geral

A aplicação simula um **cluster heterogêneo** que processa um lote fechado de tarefas.
Os nós (*workers*) fazem **balanceamento de carga por *work-stealing***: transferem lotes
de tarefas entre si dinamicamente, sem um coordenador central.

Por causa dessa comunicação constante, tentar observar "quantas tarefas existem no
sistema" somando o estado de cada nó **quase nunca dá o valor correto** — sempre há
lotes *em trânsito* na rede, invisíveis para qualquer nó isolado.

O **Snapshot Distribuído** resolve isso: ele captura um **estado global consistente**
(estado local de cada processo **+** as mensagens em trânsito nos canais) sem parar o
sistema. Com ele demonstramos duas aplicações:

- **Conservação:** a soma de tarefas (`pendentes + concluídas + em trânsito`) é sempre
  igual ao total inicial. Uma leitura ingênua erra; o snapshot acerta.
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
- **TCP de controle** Monitor → worker: comando de iniciar snapshot.
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
WORKERS=6 TAREFAS=100000 TOPOLOGIA=mesh ./run.sh
```

- `WORKERS` — número de workers (use **≥ 3**)
- `TAREFAS` — total de tarefas do lote
- `TOPOLOGIA` — `mesh` (todos conectados) ou `ring` (anel)

### Forma manual (dois terminais)

Rode os dois **no mesmo diretório** (eles compartilham `cluster.json` e `snapshots/`):

```bash
# Terminal 1 — sobe o cluster e escreve cluster.json
./build/bin/orquestrador --workers 6 --tarefas 100000 --topologia mesh

# Terminal 2 — abre o Monitor (lê cluster.json)
./build/bin/monitor cluster.json
```

`Ctrl-C` no orquestrador encerra todos os workers.

#### Opções do orquestrador

```
--workers N            (obrigatório) número de workers
--tarefas 100000       total de tarefas do lote
--topologia ring|mesh  topologia de comunicação (padrão: ring)
--porta-base 7001      porta TCP de negócio do 1º worker (= identidade)
--controle-base 8001   porta TCP de controle do 1º worker
--telemetria 9000      porta UDP de telemetria do Monitor
--host 127.0.0.1       host de bind/roteamento
--distribuir           distribui a carga entre os nós (padrão: tudo no nó 0)
--entidade <caminho>   caminho do binário entidade (padrão: mesmo diretório)
```

O `entidade` **não** é executado à mão — o orquestrador o lança com os argumentos corretos.

---

## 6. Usando o Monitor (o cenário da demonstração)

Na janela do Monitor:

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

**Roteiro sugerido:** deixe o sistema rodando, clique em **Pausar** e observe a soma
incorreta; depois clique em **Capturar Snapshot** e observe a soma exata. Espere o lote
drenar e capture de novo para ver o status mudar para **TERMINADO**.

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
