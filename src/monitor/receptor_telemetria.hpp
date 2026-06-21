// Receptor UDP da telemetria dos workers + estado agregado do cluster.
//
// Uma thread de fundo recebe os datagramas e atualiza o EstadoCluster (estado
// por worker + log de eventos). A UI le copias thread-safe a cada frame.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "comum/tipos.hpp"
#include "rede/protocolo.hpp"
#include "rede/telemetria_udp.hpp"

namespace monitor {

struct EstadoWorker {
    comum::NodeId   id          = 0;
    std::uint16_t   fator       = 0;
    comum::Contagem pendentes   = 0;
    comum::Contagem concluidas  = 0;
    bool            em_snapshot = false;
    std::uint32_t   ultimo_seq  = 0;  // descarta pacotes UDP atrasados/duplicados
};

// Estado compartilhado: a thread receptora escreve, a UI le copias.
class EstadoCluster {
public:
    void aplicar_estado(const rede::MsgTeleEstado&);
    void aplicar_evento(std::string linha);

    std::map<comum::NodeId, EstadoWorker> copia_workers() const;
    std::vector<std::string>              copia_log() const;

private:
    mutable std::mutex                    m_;
    std::map<comum::NodeId, EstadoWorker> workers_;
    std::deque<std::string>               log_;
};

class ReceptorTelemetria {
public:
    ReceptorTelemetria(comum::Porta porta, EstadoCluster& estado);
    ~ReceptorTelemetria();

    ReceptorTelemetria(const ReceptorTelemetria&) = delete;
    ReceptorTelemetria& operator=(const ReceptorTelemetria&) = delete;

    void iniciar();
    void parar();

private:
    void laco();

    rede::TelemetriaReceptor receptor_;
    EstadoCluster&           estado_;
    std::thread              th_;
    std::atomic<bool>        parado_{false};
};

}  // namespace monitor
