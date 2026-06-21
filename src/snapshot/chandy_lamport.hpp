// Building block: Snapshot Distribuido (algoritmo de Chandy-Lamport).
//
// Motor PURO do algoritmo: nao conhece tarefas, sockets nem JSON. O conteudo
// (estado local e mensagens de canal) e tratado como blobs opacos (rede::Buffer)
// que o app codifica/decodifica; o transporte (envio de marcadores, captura do
// estado local, entrega da peca pronta) entra por callbacks injetados. Assim o
// bloco e um Chandy-Lamport reutilizavel e testavel isoladamente.
//
// Pre-requisitos do algoritmo: canais FIFO (TCP) e ausencia de falhas.
//
// CONCORRENCIA: NAO e thread-safe por design. TODOS os metodos e callbacks sao
// invocados pela UNICA thread consumidora do worker -- o que torna a "gravacao
// do estado local" um corte limpo (nenhuma outra thread muta o estado) e
// dispensa locks. Suporta multiplos snapshots concorrentes (chaveados por id).
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "comum/tipos.hpp"
#include "rede/protocolo.hpp"  // rede::Buffer

namespace snapshot {

// A peca local que ESTE processo captura para um snapshot: seu estado local
// mais o estado (mensagens em transito) de cada canal de entrada.
struct PecaLocal {
    comum::SnapshotId id;
    comum::NodeId     no;
    rede::Buffer      estado_local;  // blob opaco (o worker interpreta)
    std::map<comum::NodeId, std::vector<rede::Buffer>> canais;  // por canal de entrada
};

class ChandyLamport {
public:
    // Callbacks injetados -- TODOS chamados na thread consumidora:
    //  - CapturarLocal:  le o estado de negocio atual e devolve um blob opaco.
    //  - EnviarMarcador: envia um MARCADOR(id) a um vizinho de saida.
    //  - AoCompletar:    a peca local ficou completa (marcador recebido em TODOS
    //                    os canais de entrada).
    using CapturarLocal  = std::function<rede::Buffer()>;
    using EnviarMarcador = std::function<void(comum::NodeId destino, comum::SnapshotId)>;
    using AoCompletar    = std::function<void(const PecaLocal&)>;

    ChandyLamport(comum::NodeId eu,
                  std::vector<comum::NodeId> canais_entrada,
                  std::vector<comum::NodeId> canais_saida,
                  CapturarLocal  capturar_local,
                  EnviarMarcador enviar_marcador,
                  AoCompletar    ao_completar);

    // Inicia um snapshot localmente (este no e o iniciador).
    void iniciar(comum::SnapshotId id);

    // Consumidora retirou um MARCADOR(id) vindo do canal 'origem'.
    void ao_receber_marcador(comum::NodeId origem, comum::SnapshotId id);

    // Consumidora retirou uma mensagem de NEGOCIO (TAREFAS/PEDIDO_ROUBO) do canal
    // 'origem'. Se algum snapshot estiver gravando esse canal, a mensagem entra
    // no estado do canal. (O app processa a mensagem normalmente A PARTE -- isto
    // apenas a registra para o snapshot.)
    void ao_receber_negocio(comum::NodeId origem, const rede::Buffer& corpo);

    // Ha algum snapshot em andamento neste processo? (para telemetria/UI)
    bool em_andamento() const { return !ativos_.empty(); }

private:
    // Estado de um snapshot em andamento neste processo.
    struct Execucao {
        rede::Buffer            estado_local;
        bool                    local_gravado = false;
        std::set<comum::NodeId> faltam;    // canais de entrada sem marcador ainda
        std::set<comum::NodeId> gravando;  // canais de entrada sendo gravados
        std::map<comum::NodeId, std::vector<rede::Buffer>> canais;
    };

    // Grava estado local, inicializa a gravacao dos canais de entrada e envia
    // marcadores nas saidas. 'canal_gatilho' = canal cujo marcador disparou o
    // inicio (seu estado e vazio); std::nullopt quando este no e o iniciador.
    void iniciar_gravacao(Execucao& ex, comum::SnapshotId id,
                          std::optional<comum::NodeId> canal_gatilho);

    // Se todos os marcadores de entrada chegaram, finaliza e entrega a peca.
    void verificar_conclusao(comum::SnapshotId id, Execucao& ex);

    comum::NodeId              eu_;
    std::vector<comum::NodeId> entradas_;
    std::vector<comum::NodeId> saidas_;
    CapturarLocal             capturar_local_;
    EnviarMarcador            enviar_marcador_;
    AoCompletar               ao_completar_;

    std::map<comum::SnapshotId, Execucao> ativos_;
    std::set<comum::SnapshotId>           concluidos_;  // ignora marcadores tardios
};

}  // namespace snapshot
