# Trabalho de Building Blocks em Computação Distribuída (INE 5418)

## Visão Geral e Requisitos
O objetivo é implementar uma aplicação distribuída funcional na qual múltiplos processos (pelo menos três) operam de forma concorrente e se comunicam através de chamadas reais de rede utilizando Berkeley Sockets.

A aplicação não deve apenas executar um algoritmo isolado, mas utilizar o building block sorteado como parte essencial do seu funcionamento. O conceito a ser implementado por todos os processos é o **Snapshot Distribuído**.

## 1. A Aplicação (Balanceamento de Carga / Work-Stealing)
O sistema simulará um cluster heterogêneo de processamento distribuído onde os nós realizam balanceamento de carga ativo, transferindo lotes de processamento entre si para evitar ociosidade. Workers possuem diferentes capacidades de computação. 
A ideia central é simular uma computação em grid, iniciamos com uma alta carga de tarefas em um ou múltiplos nós, não existe um nó central que coordena as tarefas e os nós se comunicam dinâmicamente para distribuir a carga. 
Devido a alta comunicação entre os nós, tentar "ver" o atual estado dos nós para medir quantas tarefas já foram concluidas e quantas ainda precisam ser processadas resulta em uma soma de tarefas diferente do lote processado devido às mensagens de rede ainda não processadas (Que se encontram em um "limbo" para os processos, nenhum processo reconhece aquele conjunto de tarefas como dele).
A função do Snapshot Distribuido é garantir a visualização correta das variáveis em determinado momento do estado do sistema, independentemente das mensagens em transito. Dessa forma, conseguimos corretamente visualizar a quantidade de processos ongoing e concluidos em um sistema distribuido, sem a necessidade de um nó central coordenando. O algoritmo usado para isso é o Chandy-Lamport. Note que para isso certas condições são necessárias, como conexão TCP entre os nós (Mecanismos contra perda de pacotes são necessários). 

* **As Entidades (Worker Nodes):** Todos os processos TCP executam exatamente o mesmo código base. Não há distinção hierárquica de papéis (simetria total). Os nós possuem características individuais sobre seu poder de processamento (num clocks/frequência), simplificado para ser apenas um fator escalar, que divide o tempo necessário por tarefa. Essas não são limitações físicas reais, apenas características utilizadas para simulação do processamento heterogêneo (Rede com diferentes computadores), que visa por em evidência o balanceamento de carga. Na prática, cada worker é um processo executando em uma porta do sistema operacional, não havendo "cores" reais para eles. Workers não conhecem do poder de processamento de outros workers, isso não é um parâmetro usado ao determinar a distribuição da carga.
* **Estado Local:** Cada processo rastreia duas variáveis locais de negócio:
  * `tarefas_pendentes`: Carga de trabalho aguardando processamento.
  * `tarefas_concluidas`: Carga de trabalho já finalizada por este nó.
* **Estado Inicial:** O sistema inicia com um lote fechado de **100.000 Tarefas** (representadas numericamente). O Orquestrador pode alocar todas no Worker 0 ou distribuí-las na inicialização, a heterogeneidade do sistema os torna quase equivalentes na distribuição de carga para um grande número de tarefas. Todos iniciam com 0 `tarefas_concluidas`.
* **A Dinâmica de Execução:** 1. **Processamento Interno:** Em loop, cada Worker "processa" seu trabalho, decrementando de `tarefas_pendentes` e incrementando em `tarefas_concluidas`. Esse processo é simulado usando as características de Clock e Cores do worker.
  1. **Transferência na Rede (Distribuição/Roubo):** Em intervalos de tempo, se um Worker tem um volume alto de `tarefas_pendentes` (ou se recebe um pedido de um vizinho ocioso), ele retira um lote de suas `tarefas_pendentes` e o envia via TCP para um nó adjacente.
* **A Restrição Matemática garantida pelo Snapshot Distribuido:** Tarefas não somem e não podem ser duplicadas. Independentemente da concorrência e do atraso de rede simulado, a soma global de (`tarefas_pendentes` + `tarefas_concluidas`) de todos os Workers **somada** às tarefas não entregues em trânsito nos Sockets TCP deve resultar em **100.000**, que é o estado inicial do sistema. Caso o valor esteja incorreto (diferente do estado inicial), o snapshot garante que há uma falha lógica no sistema, como uma condição de corrida em que duas threads processaram a mesma tarefa.

## 2. Arquitetura do Sistema e Comunicação
A arquitetura seguirá uma topologia estática para garantir o estado inicial bem definido do algoritmo.

* **Orquestrador (Entry point único em C++):** A inicialização da aplicação ocorre através de um processo orquestrador. A quantidade de entidades de cada tipo é passada por linha de comando.
* **Múltiplos Processos:** O Orquestrador será responsável por spawnar novos processos independentes no Sistema Operacional para cada entidade (atendendo ao requisito mínimo de 3 processos).
* **Identidade vs. Localização:** A **identidade** de uma entidade (`NodeId`) é a sua **porta TCP de negócio** — o orquestrador já escolhe as portas, então a identidade vem de graça, sem nenhum registro de id à parte. O **host** (em `host:porta`) é apenas roteamento. Como um único orquestrador escolhe todas as portas, ele as mantém únicas no cluster inteiro; assim a porta identifica o nó **mesmo entre máquinas diferentes**. Na execução local todos os hosts são `127.0.0.1`; para distribuir basta preencher os hosts reais — não muda o protocolo, o `NodeId` nem a lógica de negócio. O único ponto inerentemente local é *como* os processos são lançados (`fork`/`exec` localmente vs. SSH/manual em um cenário distribuído).
* **Registro de vizinhos:** Para snapshots distribuidos é um pré requisito a conectividade do grafo (Idealmente um componente fortemente conexo, permitindo o inicio do snapshot a partir de qualquer nó). Cada entidade precisa ter um método para registrar seus vizinhos e garantir a conectibilidade do grafo. É responsabilidade do orquestrador registrar os vizinhos.
* **Camada de Comunicação de Negócio (Berkeley Sockets):** Toda comunicação entre processos e inserção de marcadores ocorre via Sockets TCP de forma Peer-to-Peer. 
* **Simulação de Latência:** Utilizaremos `usleep` no recebimento dos sockets. Os processos armazenarão as requisições recebidas em uma fila interna e as processarão com atraso programado, simulando condições de rede imperfeitas.

## 3. O Building Block: Snapshot Distribuído
A aplicação implementará o algoritmo de Chandy-Lamport para capturar um estado global consistente sem interromper o sistema.

1. **Ação:** O Snapshot captura simultaneamente o **Estado Local** (variáveis dos processos) e o **Estado dos Canais** (mensagens voando na rede).
2. **Marcadores:** O Snapshot é iniciado disparando mensagens especiais chamadas "Marcadores". Um processo inicia a captura do snapshot e então distribui o marcador pela rede (para seus vizinhos). O que o algoritmo garante é que todos os processos que receberem o marcador irão gravar seu snapshot.
3. **Persistência:** O resultado do estado capturado será salvo como arquivos locais em formato JSON. A aplicação terá a capacidade de carregar e visualizar esses estados.
4. **Agregação:** Não é responsabilidade do Chandy-Lamport agregar os snapshots. Como falado, ele apenas garante que os processos capturem seus snapshots. A agregação é inerentemente uma camada acima da arquitetura, e no nosso caso, ela será feita em conjunto com o Monitor, independente do restante do código.

## 4. Monitoramento e Interface (ImGui)
Para atender ao requisito de "observar claramente o funcionamento do mecanismo", haverá um processo especial chamado `Monitor`. 

* **Telemetria (UDP):** O Monitor renderiza a interface em ImGui. Os processos de negócio enviarão pacotes UDP simples (Fire-and-forget) informando suas ações e informações. Isso garante que o monitoramento não sujeita a rede TCP principal a travamentos.
* **Tabelas Visuais:** O ImGui exibirá logs de ações em tempo real. Cada processo irá ter uma barra visual relacionando a quantidade de processos totais e concluidos, que atualizará em tempo real conforme o work-stealing ocorrer. Também será possível visualizar a capacidade de processamento (ex: x5) no monitor, tal qual as mensagens trocadas pelos processos.

### O Cenário de Demonstração (Atendendo ao requisito de "Atraso/Concorrência") 
A interface terá dois controles contrastantes para provar a tese de sistemas distribuídos:
1. **Botão de Pausa (O Caos):** Tenta pausar a tela instantaneamente para ver a soma das variáveis ativas via UDP. Devido ao `usleep` nas filas de socket, o valor total apresentado será idealmente incorreto.
2. **Botão Capturar Snapshot (A Ordem):** Engatilha a injeção do Marcador TCP (em um processo qualquer). Mesmo com a latência e o sistema rodando, a leitura do arquivo JSON do snapshot reconstruirá perfeitamente as variáveis, demonstrando a integridade dos processos mesmo em meio a comunicações.

## 5. Checklist de Entregas (Moodle)
Para a finalização do projeto, o grupo deve garantir as seguintes entregas, com o nome de todos os participantes:
- [ ] Implementação da aplicação funcional e do building block.
- [ ] Código-fonte consolidado.
- [ ] Documento (README) com instruções claras de compilação e execução.
- [ ] Slides formatados para o seminário.
- [ ] Link do vídeo de demonstração (máximo de 10 minutos) apresentando logs, execução normal e o cenário especial de latência/snapshot.

## 6. Estrutura do Seminário (10 a 12 minutos) 
A apresentação presencial deverá cobrir rigorosamente os seguintes tópicos solicitados na especificação:
1. O problema estudado e aplicação implementada.
2. O Building block utilizado, seu algoritmo interno e API disponibilizada.
3. A arquitetura da solução (A separação entre Orquestrador, ImGui e Processos TCP).
4. Ilustração do uso prático da aplicação.
5. Demonstração do cenário de atraso/concorrência vs. Snapshot consistente.
6. Principais dificuldades técnicas de implementação (C++, Sockets, ImGui).
7. Limitações encontradas e conclusões.