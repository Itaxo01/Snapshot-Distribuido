// Coletor de snapshots (lado Monitor) -- coleta via FILESYSTEM.
//
// Disparar um snapshot e fire-and-forget: o Monitor atribui um SnapshotId, envia
// INICIAR_SNAPSHOT ao iniciador pela conexao de controle e retorna na hora. Isso
// nao bloqueia a UI nem serializa snapshots -- varios podem estar em andamento
// ao mesmo tempo (o algoritmo suporta snapshots concorrentes).
//
// Uma thread VARREDORA observa a pasta snapshots/. Cada worker publica sua peca
// em snapshots/<id>/<no>.json (escrita atomica). Quando a pasta de um <id> tem
// as N pecas, a varredora agrega, valida a conservacao (soma == total), grava
// snapshots/<id>/final_snapshot.json e o resultado "aparece" na lista da UI.
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "comum/tipos.hpp"
#include "rede/socket_tcp.hpp"

namespace monitor {

struct EndpointCtrl {
    comum::NodeId   id;
    comum::Endereco controle;
};

struct PecaColetada {
    comum::NodeId   no          = 0;
    comum::Contagem pendentes   = 0;
    comum::Contagem concluidas  = 0;
    comum::Contagem em_transito = 0;  // soma das tarefas em transito nos canais
};

struct ResultadoSnapshot {
    comum::SnapshotId         id          = 0;
    comum::NodeId             iniciador   = 0;  // == id >> 32
    std::vector<PecaColetada> pecas;
    long long                 soma        = 0;
    long long                 total       = 0;
    bool                      consistente = false;  // soma == total
    bool                      sessao      = false;  // disparado nesta execucao
    std::string               caminho;             // final_snapshot.json

    // Deteccao de termino (aplicacao do snapshot): o sistema terminou quando
    // NENHUM no tem tarefas pendentes E NENHUM canal tem tarefas em transito.
    // O snapshot detecta isso corretamente mesmo quando a leitura ingenua (todos
    // ociosos via UDP) erraria por causa de um lote voando na rede.
    long long pendentes_totais = 0;
    long long transito_totais  = 0;
    bool      terminado        = false;
};

class Coletor {
public:
    explicit Coletor(std::vector<EndpointCtrl> workers);
    ~Coletor();

    Coletor(const Coletor&) = delete;
    Coletor& operator=(const Coletor&) = delete;

    void conectar();  // abre as conexoes de controle (com retry)
    void iniciar();   // inicia a thread varredora
    void parar();

    // Dispara um snapshot (assincrono, nao bloqueia). Retorna o id atribuido.
    comum::SnapshotId disparar(comum::NodeId iniciador);

    // Injeta 'quantidade' tarefas no worker 'alvo' (mensagem de controle) e soma
    // ao total de referencia do cluster. Retorna false se o alvo nao existe ou o
    // envio falhar (nesse caso o total NAO e alterado).
    bool atribuir_tarefas(comum::NodeId alvo, comum::Contagem quantidade);

    // Total de tarefas injetadas ate agora (referencia da conservacao).
    comum::Contagem total() const { return total_.load(std::memory_order_relaxed); }

    const std::vector<EndpointCtrl>& workers() const { return workers_; }

    // Resultados completos, separados por origem (mais recentes primeiro).
    std::vector<ResultadoSnapshot> resultados_sessao() const;
    std::vector<ResultadoSnapshot> resultados_anteriores() const;

private:
    void      varrer_uma_vez();
    bool      agregar(comum::SnapshotId, ResultadoSnapshot& fora) const;
    long long total_referencia(const std::string& dir) const;
    int       indice_de(comum::NodeId) const;

    std::vector<EndpointCtrl>    workers_;
    std::atomic<comum::Contagem> total_{0};  // tarefas injetadas em runtime
    std::vector<rede::Socket>    conns_;      // paralelo a workers_
    std::uint32_t             seq_ = 0;

    std::thread       th_;
    std::atomic<bool> parado_{false};

    mutable std::mutex                            m_;
    std::set<comum::SnapshotId>                   disparados_;  // ids desta sessao
    std::map<comum::SnapshotId, ResultadoSnapshot> prontos_;    // ja agregados
};

}  // namespace monitor
