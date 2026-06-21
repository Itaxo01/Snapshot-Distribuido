// Worker: a entidade do cluster. Roda a UNICA thread consumidora que processa
// tarefas, faz work-stealing, dirige o motor de snapshot e emite telemetria.
//
// Threads no processo: esta consumidora (unica mutadora de estado), N leitoras
// de canal e 1 de controle. Produtoras (leitoras/controle) so empurram em filas
// thread-safe; a consumidora e a unica a tocar pendentes/concluidas/engine.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "comum/config.hpp"
#include "comum/tipos.hpp"
#include "entidade/controle.hpp"
#include "entidade/estado_negocio.hpp"
#include "entidade/work_stealing.hpp"
#include "rede/canal.hpp"
#include "rede/protocolo.hpp"
#include "rede/socket_tcp.hpp"
#include "rede/telemetria_udp.hpp"
#include "snapshot/chandy_lamport.hpp"

namespace entidade {

class Worker {
public:
    explicit Worker(comum::ConfigWorker cfg);

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void preparar();  // bind + estabelece canais + inicia controle
    void executar();  // laco consumidor; retorna quando parar() e chamado
    void parar();     // sinaliza o fim (thread-safe; ex.: handler de sinal)

private:
    using Relogio = std::chrono::steady_clock;

    static std::vector<comum::NodeId> ids_de(const std::vector<comum::Vizinho>&);

    // Callbacks do motor de snapshot (rodam na thread consumidora).
    rede::Buffer capturar_local();
    void         enviar_marcador(comum::NodeId destino, comum::SnapshotId);
    void         ao_completar(const snapshot::PecaLocal&);

    // Passos do laco consumidor.
    void drenar_comandos();
    void despachar(const rede::MensagemRecebida&);
    void processar_tarefas();
    void distribuir();
    void emitir_estado();

    // Auxiliares.
    void          registrar_canal(std::unique_ptr<rede::Canal>);
    void          enviar_para(comum::NodeId destino, const rede::Buffer& corpo);
    comum::NodeId vizinho_aleatorio();
    void evento(rede::Evento, comum::NodeId peer, comum::Contagem valor,
                comum::SnapshotId);
    void salvar_peca_local(comum::SnapshotId, comum::NodeId, const std::string& json);

    comum::ConfigWorker  cfg_;
    const ParametrosRoubo params_;
    EstadoNegocio        estado_;

    rede::FilaInbox      inbox_;
    rede::Socket         servidor_negocio_;
    std::vector<std::unique_ptr<rede::Canal>> canais_;
    std::map<comum::NodeId, rede::Canal*>     canal_por_no_;
    std::vector<comum::NodeId>                ids_vizinhos_;

    snapshot::ChandyLamport engine_;
    Controle                controle_;
    rede::TelemetriaEmissor telemetria_;

    std::atomic<bool> rodando_{true};
    std::uint32_t     seq_estado_ = 0;
    std::uint32_t     seq_ev_     = 0;
    std::mt19937      rng_{std::random_device{}()};

    Relogio::time_point ultimo_processamento_{};
    Relogio::time_point prox_dist_{};
    Relogio::time_point prox_tele_{};
};

}  // namespace entidade
