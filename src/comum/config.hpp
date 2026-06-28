// Configuracao de implantacao (deployment) que o orquestrador entrega a cada
// processo. Concentra TODO o enderecamento de rede em um so lugar.
//
// Portavel entre maquinas: a identidade (porta) e a localizacao (host) vivem no
// mesmo Endereco, mas so o host muda ao distribuir. Em execucao local todos os
// hosts sao "127.0.0.1"; para maquinas diferentes basta preencher os hosts reais
// -- nem o protocolo nem a logica dos workers mudam. O unico ponto inerentemente
// local e COMO os processos sao lancados (fork/exec vs. SSH/manual), que nao
// vive aqui.
#pragma once

#include <cstdint>
#include <vector>

#include "comum/tipos.hpp"

namespace comum {

// Um vizinho e simplesmente o endereco de negocio de outro worker; sua
// identidade (NodeId) e o proprio campo .porta.
using Vizinho = Endereco;

// Configuracao de uma entidade (worker). Seu NodeId == escuta.porta.
struct ConfigWorker {
    Endereco escuta;   // onde este worker aceita conexoes TCP de negocio
                       //   .porta = identidade do no
                       //   .host  = interface de bind: "127.0.0.1" (local) ou
                       //            "0.0.0.0" (aceitar de outras maquinas)
    Porta    porta_controle = 0;  // onde aceita a conexao de controle do Monitor

    std::vector<Vizinho> vizinhos;  // arestas de saida; o grafo deve ser
                                    // fortemente conexo (pre-req do snapshot)

    Endereco monitor_telemetria;  // destino UDP da telemetria (host:porta)

    std::uint16_t   fator = 1;  // fator de clock (heterogeneidade)
    // Sem carga inicial: as tarefas sao injetadas em runtime pelo Monitor.

    NodeId id() const { return escuta.porta; }
};

// Configuracao do Monitor. Precisa alcancar cada worker (conexao de controle) e
// escutar a telemetria UDP. Nao participa do grafo de negocio.
struct EndpointWorker {
    NodeId   id;        // identidade do worker (== sua porta de negocio)
    Endereco controle;  // onde o Monitor conecta para enviar comandos / receber
                        // a peca de snapshot (porta de controle, distinta da de
                        // negocio)
};

struct ConfigMonitor {
    Porta                       porta_telemetria = 0;  // bind UDP da telemetria
    std::vector<EndpointWorker> workers;               // todos os workers a observar
};

}  // namespace comum
