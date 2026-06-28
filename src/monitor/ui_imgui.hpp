// Interface ImGui do Monitor: barras por worker, log, e os botoes Pausa (caos)
// vs. Capturar Snapshot (ordem). Layout em duas colunas.
#pragma once

#include <map>
#include <string>

#include "comum/tipos.hpp"
#include "monitor/coletor_snapshot.hpp"
#include "monitor/receptor_telemetria.hpp"

namespace monitor {

struct ContextoUI {
    EstadoCluster&  estado;
    Coletor&        coletor;
    std::string     info;  // ex.: "4 workers"

    // estado interno da UI
    bool                                  pausado        = false;
    std::map<comum::NodeId, EstadoWorker> congelado;       // leitura congelada no pause
    long long                             soma_congelada = 0;
    int                                   iniciador_idx  = 0;
    int                                   alvo_idx       = 0;     // worker p/ injetar
    int                                   qtd_injetar    = 10000; // tarefas a injetar
};

void desenhar(ContextoUI& ctx);

}  // namespace monitor
