// Conexao de controle Monitor -> worker (SOMENTE recepcao).
//
// Uma thread de fundo aceita a conexao do Monitor e le comandos (INICIAR_SNAPSHOT),
// empurrando-os para uma fila que a thread consumidora do worker drena. As pecas
// de snapshot prontas sao PUBLICADAS EM ARQUIVO (a coleta e via filesystem, feita
// pelo Monitor), nao por esta conexao. Telemetria UDP e um caminho separado.
#pragma once

#include <atomic>
#include <thread>

#include "comum/tipos.hpp"
#include "rede/fila_bloqueante.hpp"
#include "rede/socket_tcp.hpp"

namespace entidade {

// Comando vindo do Monitor para a thread consumidora.
struct Comando {
    enum class Tipo { INICIAR_SNAPSHOT, ATRIBUIR_TAREFAS } tipo;
    comum::SnapshotId snapshot_id = 0;  // INICIAR_SNAPSHOT
    comum::Contagem   quantidade  = 0;  // ATRIBUIR_TAREFAS
};

class Controle {
public:
    explicit Controle(const comum::Endereco& escuta_controle);  // cria o listener
    ~Controle();

    Controle(const Controle&) = delete;
    Controle& operator=(const Controle&) = delete;

    void iniciar();  // inicia a thread de aceitar + ler comandos
    void parar();    // idempotente

    rede::FilaBloqueante<Comando>& comandos() { return comandos_; }

private:
    void laco();

    rede::Socket                  servidor_;
    std::atomic<int>              fd_conexao_{-1};  // fd da conexao do Monitor
    rede::Socket                  conexao_;         // posse (RAII) da conexao
    std::thread                   th_;
    std::atomic<bool>             parado_{false};
    rede::FilaBloqueante<Comando> comandos_;
};

}  // namespace entidade
