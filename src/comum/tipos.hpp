// Tipos compartilhados por todas as camadas do sistema.
//
// Convencoes do projeto:
//  - A identidade de um no E a sua porta TCP de negocio (NodeId == Porta). O
//    orquestrador ja escolhe as portas, entao a identidade vem de graca, sem
//    nenhum registro de id separado. O Endereco (host + porta) carrega a
//    localizacao; o host e so roteamento.
//  - Como um unico orquestrador escolhe TODAS as portas, ele as mantem unicas no
//    cluster inteiro. Assim a porta identifica o no mesmo entre maquinas
//    diferentes (nunca ha maquinaA:5000 e maquinaB:5000 ao mesmo tempo).
//  - Contagens de tarefas cabem em 32 bits.
//  - O SnapshotId empacota o worker iniciador nos 32 bits altos e um contador temporal
//    nos 32 bits baixos, garantindo unicidade global SEM coordenacao central.

#pragma once

#include <cstdint>
#include <string>

namespace comum {

using Porta    = std::uint16_t;
using NodeId   = Porta;          // identidade do no == sua porta TCP de negocio
using Contagem = std::uint32_t;  // tarefas pendentes / concluidas / em lote

// Localizacao de um no na rede. Em execucao local, host == "127.0.0.1"; em
// execucao distribuida, host e o IP/hostname da maquina daquele no. Separar o
// Endereco do NodeId e o que torna o sistema portavel entre maquinas.
struct Endereco {
    std::string host;
    Porta       porta = 0;
};

// Identificador de um snapshot Chandy-Lamport.
// Empacotamento: (iniciador << 32) | seq.
//  - iniciador: ID do no (ou do Monitor) que disparou o snapshot.
//  - seq:       contador local de quem disparou.
// Dois iniciadores distintos nunca colidem; o mesmo iniciador distingue seus
// snapshots pelo seq. Assim multiplos snapshots concorrentes coexistem.
using SnapshotId = std::uint64_t;

constexpr SnapshotId fazer_snapshot_id(NodeId iniciador, std::uint32_t seq) {
    return (static_cast<SnapshotId>(iniciador) << 32) | seq;
}

constexpr NodeId snapshot_iniciador(SnapshotId id) {
    return static_cast<NodeId>(id >> 32);
}

constexpr std::uint32_t snapshot_seq(SnapshotId id) {
    return static_cast<std::uint32_t>(id & 0xFFFFFFFFu);
}

// Diretorio (relativo ao CWD) onde as pecas de um snapshot sao salvas. Workers
// gravam <pasta>/<node_id>.json; o Monitor grava <pasta>/final_snapshot.json.
inline std::string pasta_snapshot(SnapshotId id) {
    return "snapshots/" + std::to_string(id);
}

}  // namespace comum
