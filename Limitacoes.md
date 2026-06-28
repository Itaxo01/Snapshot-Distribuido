# Limitações conhecidas e *trade-offs*

Decisões de projeto que limitam a aplicação, com o motivo de cada escolha, o que se
ganha/perde e a alternativa correspondente. Várias destas **não** são limitações do
algoritmo de Chandy–Lamport em si, mas escolhas de engenharia feitas para simplificar a
implementação e a demonstração.

---

## 1. Iniciação do snapshot: entidade central vs. snapshot espontâneo

**O que fizemos.** O snapshot é disparado por uma entidade central — o **Monitor**, que se
conecta à porta de *controle* de cada worker e envia o comando de iniciar. Isso dá um botão
"Capturar Snapshot" e controle total sobre *quando* o snapshot acontece, o que é ótimo para
a visualização e a demonstração.

**Por que é só comodidade.** Chandy–Lamport **não** exige um iniciador central: qualquer
processo pode iniciar um snapshot espontaneamente (por tempo, por uma condição local, etc.)
e o algoritmo funciona de forma idêntica. O marcador carrega o *snapshot id*; vários
iniciadores espontâneos simplesmente geram **snapshots concorrentes** — que nosso motor já
suporta.

**Trade-off.**
- *Iniciação central:* simples de controlar e de observar, mas introduz um ponto que decide
  os snapshots (o Monitor). A propriedade descentralizada do algoritmo continua intacta — o
  Monitor é apenas um *gatilho* + *coletor*, não participa da correção do algoritmo —, mas
  nós não a exercemos a partir dos workers.
- *Iniciação espontânea:* totalmente descentralizada, sem ponto central de disparo, mas
  exige uma **política** de quando cada nó inicia e é mais difícil de observar/demonstrar.

---

## 2. Uma thread consumidora vs. múltiplas threads consumidoras

**O que fizemos.** Cada worker tem **uma** thread consumidora (única que altera o estado de
negócio), **N** threads produtoras (uma por canal de entrada) e **uma** thread de controle.
Com um único *mutador*, o corte do snapshot é um ponto limpo entre o processamento de duas
mensagens, **sem locks**, e as regras do marcador executam atomicamente de graça.

**Quando N consumidoras seriam possíveis.** Só com três garantias:
1. **Ordem FIFO** por canal preservada;
2. **Acordo atômico** no início do snapshot e na desativação de um canal de entrada
   (lado da recepção);
3. **Encaminhamento atômico** do marcador para todos os canais de saída **antes** de
   processar/enviar mais mensagens de aplicação naqueles canais (lado do envio — o ponto
   mais fácil de esquecer).

**Trade-off.** Essas garantias forçam **locks/barreiras** que bloqueiam o trabalho a cada
snapshot. As seções críticas acabam cobrindo quase todo o trabalho útil (ainda mais num
*workload* que só incrementa contadores), então paga-se a complexidade de sincronização por
**pouco ganho de paralelismo** — e arrisca-se introduzir justamente as condições de corrida
que o snapshot existe para detectar.

**Alternativa recomendada.** Em vez de mais threads consumidoras por nó, **manter uma
consumidora e duplicar os processos** — ou seja, escalar adicionando **mais nós
single-threaded**, não mais threads por nó. A unidade natural de paralelismo em
Chandy–Lamport é o **participante (processo)**, não a thread; assim cada worker permanece
**lock-free** durante o snapshot. É como sistemas reais escalam: o **Apache Flink**
paraleliza fazendo cada subtarefa ser seu próprio participante, com *barrier alignment*, em
vez de várias threads compartilhando o estado de um nó.
*Custo da alternativa:* mais processos = mais conexões e mais sobrecarga do SO; para trabalho
realmente CPU-bound, threads de verdade seriam desejáveis — mas para o nosso modelo (e para
sistemas de *streaming* em geral) mais participantes é o caminho idiomático.

---

## 3. Topologia estática (sem adição de workers em tempo de execução)

**O que fizemos.** O grafo de comunicação é **fixo na inicialização**. Cada worker precisa
conhecer o endereço de **todos** os seus vizinhos potenciais ao subir, e há uma janela de
espera (definida por `conectar_tcp`, ~5 s) para o vizinho aparecer; `preparar()` só termina
quando todos os canais foram estabelecidos. **Não** é possível adicionar um worker no meio
da execução.

**Por que é proposital.**
- Chandy–Lamport **pressupõe uma rede estática** do início ao fim de um snapshot. Inserir
  nós no meio pode quebrar o algoritmo: a conclusão local depende de receber um marcador em
  **todos** os canais de entrada (conjunto fixo), e a contabilidade de conservação/total
  pressupõe uma *membership* fixa.
- Reconhecer e integrar workers em tempo de execução é possível, mas **invade a área de
  outro building block: *Group Membership* (view synchrony)**.

**Alternativa: compor com Group Membership.**
Sob uma camada de *group membership*, a adição dinâmica passa a ser suportável — mas **apenas
porque as mudanças de membership acontecem em fronteiras bem definidas e totalmente
ordenadas (views)**. Pontos-chave:
- Um snapshot ocorre **inteiramente dentro de uma view** — um **grupo bem definido** de
  processos.
- Adicionar um nó **no meio de um snapshot não altera** o snapshot em andamento: aquele nó
  simplesmente **não é considerado** por ele; **apenas snapshots posteriores** (na nova
  view) passam a incluí-lo.
- A *view synchrony* faz da própria troca de view um corte consistente (canais quiescentes,
  canais novos começam vazios), então cada snapshot fica bem definido por view.

**Trade-off.** Topologia estática = mais simples e é uma **pré-condição correta** do building
block escolhido. Suporte dinâmico = exige **compor um segundo building block** (group
membership), com toda a maquinaria de acordo sobre views, entrega *view-synchronous* e o
tratamento de um snapshot em andamento numa troca de view (abortar+reiniciar, ou adiar a
troca).

---

## 4. Coleta dos snapshots via filesystem compartilhado

**O que fizemos.** Cada worker publica sua peça em `snapshots/<id>/<no>.json` (escrita
atômica: grava em `.tmp` e renomeia), e o Monitor **varre a pasta** e agrega quando as `N`
peças de um id estão presentes. Funciona porque, rodando localmente, Monitor e workers
**compartilham o filesystem**.

**Trade-off.** Simples, desacopla o disparo da coleta e suporta snapshots concorrentes
naturalmente (sem caminho de rede dedicado). Em compensação, **depende de um FS compartilhado**
(execução local). Para máquinas diferentes seria preciso outra estratégia de coleta — por
isso a mensagem `PECA_SNAPSHOT` foi mantida (reservada) no protocolo, para a alternativa
"Monitor como coletor de rede".

**Alternativas para um ambiente distribuído real:**

1. **Coletor centralizado via rede (o padrão mais comum).** Cada processo, ao terminar seu
   snapshot local, envia seu pedaço por uma conexão de rede dedicada a um *coletor* —
   tipicamente o iniciador ou um observador externo. O coletor conhece a *membership* (`N`) e
   só fecha o snapshot quando recebe os `N` pedaços. Ponto-chave: o pedaço viaja por um canal
   **fora** das arestas TCP de negócio, para não contaminar os canais capturados.
   *Exemplo real:* **Apache Flink** (checkpointing por *barriers*, variante do Chandy–Lamport):
   o **JobManager** coordena, o checkpoint só é "completo" quando todos os *acks* chegam, e o
   estado pesado vai para storage durável (RocksDB/HDFS/S3).
2. **Convergecast em árvore de agregação.** Os pedaços sobem por uma árvore até a raiz; cada
   nó intermediário já combina os filhos antes de repassar. Evita que um único coletor seja
   gargalo com milhares de nós.
3. **Storage de objetos compartilhado (S3/HDFS/GCS).** Conceitualmente igual ao nosso
   diretório, mas o "lugar comum" é um storage distribuído acessível por todas as máquinas.
   Cada processo faz *upload* do seu pedaço e um leitor agrega depois.
4. **Log/stream replicado (Kafka, ou log via Raft).** Cada processo publica seu pedaço como
   uma mensagem com o snapshot id como chave; um consumidor agrega ao ver os `N` pedaços.
   Ganha durabilidade, replicação e ordenação da infraestrutura.
5. **Registro/coordenador de metadados (etcd/ZooKeeper).** Os processos escrevem pedaços (ou
   ponteiros) sob um prefixo do snapshot id, e o coletor faz *watch* para ser notificado
   quando os `N` chegarem (substitui o *polling* da pasta por notificação consistente).

**Dois invariantes se repetem em qualquer abordagem:** (a) quem agrega precisa conhecer a
*membership* (`N`) e esperar o "completo" de todos; (b) a publicação de cada pedaço precisa
ser **atômica** do ponto de vista do leitor — no FS é `write .tmp + rename`; no S3 é a
atomicidade do PUT; no Kafka/etcd é a do *append*/escrita da chave. O algoritmo de
Chandy–Lamport só produz os pedaços distribuídos; **a coleta é sempre uma camada de
engenharia acima dele**.

---

## 5. Injeção de carga em runtime vs. snapshot: janela transitória

**O que fizemos.** Os workers iniciam vazios e a carga é injetada em runtime pelo Monitor
(comando de controle `ATRIBUIR_TAREFAS`). O Monitor mantém o **total de referência** do
cluster como a soma do que foi injetado, e o usa na verificação de conservação
(`soma == total`). Cada snapshot **congela** o total de referência no momento de sua
agregação (o coletor agrega cada `id` uma única vez e o mantém em cache), e snapshots de
execuções anteriores preservam o total persistido em seu `final_snapshot.json`.

**A limitação.** A injeção viaja pela conexão de **controle** Monitor→worker, que **não**
faz parte do grafo de negócio capturado pelo snapshot. Entre "o Monitor soma `X` ao total"
e "o worker-alvo aplica `X` às suas pendentes" há uma pequena janela. Se um snapshot for
capturado *exatamente* nessa janela, a tarefa injetada não está nem no estado local de
nenhum nó nem em um canal capturado (está em trânsito na conexão de controle, invisível ao
corte) — e o snapshot aparece **transitoriamente inconsistente** (`soma < total`).

**Por que não é resolvido afunilando tudo numa fila.** O determinante é que injeção e
marcador chegam ao worker por **conexões TCP distintas** (controle vs. negócio), e FIFO é
*por-conexão*: não há ordenação causal entre "aplicar injeção" e "gravar o corte". Jogar
ambas na mesma fila do consumidor não cria a ordenação que o transporte não deu. A correção
robusta seria tratar a injeção como **mensagem de negócio que entra no grafo** (sujeita à
disciplina de marcadores) ou tornar o Monitor um participante lógico cujos canais de
controle sejam capturados — fora do escopo desta demonstração.

**Mitigação adotada (pragmática).** Injete a carga e **deixe-a assentar** antes de capturar
(as barras de telemetria refletem a aplicação em poucos milissegundos). Como cada snapshot
congela seu total na agregação, capturar fora da janela de injeção sempre dá `soma == total`.
