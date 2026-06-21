O Desafio Central: Definindo um Estado Global Consistente

	Gostaríamos de capturar um estado global dos processos:
		Tirar uma “Fotografia” do estado atual de todo o sistema
			Travar os processos, salvar o valor de todas as variáveis de cada processo + mensagens em rede, e continuar como se nada tivesse acontecido.

	Pois assim, conseguimos:
		Criar pontos de restauração para o sistema, combinados com log ou não 
			Ex: Save states em um ROM emulator (distribuido em PokeMMO)
		Coleta de lixo, identificar regiões inalcançáveis do sistema 
			Não são referenciadas ou não referenciam nenhum outro nó
		Verificação de invariantes globais
			Quando o estado do sistema ou de alguma variável precisa se manter constante em referência a algo.
		Detecção de Deadlocks 
			Se nessa foto percebermos que existe uma relação de “A espera B, B espera C, C espera A”, podemos concluir que há um deadlock entre múltiplos processos.
		Detecção de término
			Conseguimos saber se todos os processos estão realmente ociosos e de que não há mensagens voando na rede.

	Por que isso é difícil?
		É impossível sincronizar todos os processos sob um relógio global
			Em uma máquina local, poderíamos fazer uma presunção de que todos os processos salvem seu estado com o clock do sistema. Em um sistema distribuido, isso não é possível.
		Processos não possuem uma memória compartilhada
			Um processo não consegue simplesmente ler o estado dos outros e salvar o que vê, tudo acontece por troca de mensagens.
		Atrasos e comunicação assíncrona
			Comunicações via rede possuem atrasos inerentes, tentar iniciar um snapshot mandando um broadcast para os processos não é possível, pois não há garantia de que os processos irão receber a mensagem ao mesmo tempo.



A Solução: Algoritmo de Chandy-Lamport

	Algoritmo de Chandy-Lamport
		Proposto em 1985 por Kanianthra Mani Chandy e Leslie Lamport
			Até então, se acreditava que a solução para capturar o estado global de um sistema envolvia um relógio global que congelasse o sistema de forma síncrona.
			Chandy e Lamport provaram matematicamente que é possível capturar o estado global sem a necessidade de um relógio global ou de congelar o sistema como um todo.

	As regras do jogo (Suposições do modelo)
		O Algoritmo não funciona sob todos os cenários, certas suposições são necessárias para garantir a corretude do algoritmo
		Suposições Fortes:
			Canais confiáveis -> Nenhuma mensagem é perdida, corrompida ou duplicada.
			Canais FIFO -> As mensagens chegam na exata ordem em que são mandadas
			Ausência de falhas -> Nenhum processo falha durante o snapshot
			Topologia -> Os processos formam um grafo fortemente conexo
		Suposições Fracas:
			Totalmente assíncrono -> Não exige limite de tempo para processamento, atraso de mensagens ou relógios sincronizados.

	Marcador (Marker):
		O marcador é o coração do algoritmo
		Se trata de uma mensagem de controle enviada para os processos, chega no mesmo canal que uma mensagem qualquer.
		Ela marca o inicio do snapshot, separando as mensagens em dois tipos:
			1. Mensagens enviadas antes do snapshot.
			2. Mensagens enviadas depois do snapshot.	

	Início do Snapshot (Nó inicializador)
		Qualquer nó pode iniciar o snapshot (Por conta de ser um GFC).
		Ao começar um snapshot, o nó:
			Salva seu estado local.
			Envia o marker para todos os seus canais de saída.
			Começa a gravar as mensagens que chegam no buffer de entrada.

	Nós receptores
		Ao receber o marcador pela primeira vez, o nó:
			Salva seu estado local.
			Marca o canal em que a mensagem chegou como vazio.
			Envia um marker para todos os seus canais de saída
			Começa a gravar as mensagens que chegam nos seus canais de entrada que ainda estão habilitados.
		Ao receber um marcador subsequente, o nó:
			Já gravou seu estado local -> flag atômica in_snapshot
            Para de gravar o canal em que a mensagem chegou -> As mensagens no canal são as mensagens que foram transmitidas após o inicio do snapshot.

	Quando sabemos que o algoritmo finalizou?
		Quando todos os canais de entrada do processo estão desabilitados -> Todos já receberam um marker, o que implica em que todos os processos que poderiam enviar algo para esse processo já realizaram ou estão realizando o snapshot.
		Note que, nesse ponto, o processo X não receberá nada de novo, e pode corretamente registrar seu snapshot.
		A conclusão do algoritmo é inerentemente independente entre os processos, não importa que o snapshot por inteiro não tenha sido concluído, cada processo só se importa com aquilo que poderia alterar o seu próprio snapshot.

	Como tratar esses snapshots?
		O paper de Chandy-Lamport trata a agregação dos snapshots como uma camada superior da infraestrutura -> O processamento dos dados fica a critério da aplicação, tal qual dar um significado a eles. O que o algoritmo garante é a corretude dos estados capturados.
		Existem certas estratégias que podem ser utilizadas para agregar os snapshots:
			Monitor central que conhece os processos e recebe o snapshot de todos (Utilizado na aplicação desenvolvida). Isso pode ser por meio de requisições (Os processos enviam seu snapshot ao monitor) ou também por meio de filesystem compartilhado (Ou alternativas em nuvem como o S3).
			Convergecast em árvore de agregação -> Os processos formam uma árvore a partir da raiz (nó inicializador), cada processo é responsável por agregar o snapshot de seus filhos. Ideal para não sobrecarregar um nó.

Aplicação: Cluster de Computação Distribuído
	Para demonstrar o algoritmo de Chandy-Lamport na prática, criamos um código que simula um cluster de computação distribuído com Work Stealing
		Processos “roubam” a carga de trabalho um dos outros.
	A aplicação consiste em três executáveis distintos:
		Worker -> Nó que executa as tarefas.
		Monitor -> Interface que permite visualizar e interagir com o sistema.
		Orquestrador -> Entrypoint para criação facilitada dos workers.
	O Orquestrador é chamado uma única vez, sua função é:
		Garantir a topologia de grafo fortemente conexo.
		Spawnar os workers corretamente.
	Após isso, as entidades que continuam funcionando são os workers e o monitor.


	Worker
		É uma entidade independente, recebe tarefas e as executa.
		Cada worker roda como um processo a parte no SO (fork).
		Se comunica de forma independente com workers vizinhos via TCP de forma bi-direcional. 
		Implementa Work Stealing. Periodicamente, roda os seguintes passos:
			Verifica a sua quantidade de tarefas pendentes;
			Se a quantidade for muito alta, joga parte das tarefas a um vizinho aleatório.
			Se a quantidade for 0 (Ocioso), pede tarefas a um vizinho aleatório.
		Possui um canal de entrada e um de saída por vizinho.
			A aplicação suporta as topologias de Mesh e Ring.
		Roda 2 + k threads, em que k é o número de vizinhos
			Uma thread por canal de entrada (K threads ao total)
			Uma thread de controle, se conecta ao Monitor (TCP, apenas recebe)
			Uma thread consumidora, processa as tarefas e lida com as mensagens recebidas (Limitações ao final).
		Manda periódicamente pacotes de telemetría UDP para o monitor

	Monitor
		Interface feita com o ImGui
		Recebe pacotes UDP contendo as informações de cada worker e as renderiza na tela em tempo real
			Mostra os logs, tarefas pendentes/concluidas de cada worker, e outras informações gerais
			Pacotes UDP podem ser perdidos na rede, ou chegarem em uma ordem diferente da enviada. O pacote UDP contém o seq, timestamp local do worker, o Monitor guarda essa informação e só considera o mais recente.
		Permite a inicialização do snapshot a partir de um worker selecionado
		Permite congelar a visualização. Isso é apenas visual, só o monitor para, o sistema continua funcionando.

	Objetivo
		Na inicialização, há um número fixo de tarefas a serem concluídas.
		Devido a constante comunicação entre os workers devido ao work-stealing, tentar visualizar a quantidade de tarefas pendentes + concluídas em todo o sistema quase nunca resulta no valor inicial.
		O botão de “pausa” do monitor serve justamente para isso, demonstrar a quantidade de tarefas de uma forma superficial, como se alguém estivesse tentando olhar para todos os processos ao mesmo tempo.
		Através do snapshot, é possível capturar com exatidão a quantidade de tarefas pendentes, concluídas e em rede
			Se tratando de uma constante, é possível utilizar esse dado para verificar se houve condição de corrida em algum worker que resultou na perda ou duplicação de alguma tarefa.
