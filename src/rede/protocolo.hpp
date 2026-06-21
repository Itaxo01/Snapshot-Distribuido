// Protocolo de fio (wire protocol) do sistema.
//
// Camada PURA: converte mensagens <-> bytes. Nao toca em sockets.
//   - O enquadramento (framing) e responsabilidade do transporte:
//       * TCP  : prefixa [u32 tamanho] ao corpo  -> ver quadro_tcp() / TAM_PREFIXO
//       * UDP  : envia o corpo como um unico datagrama (auto-delimitado)
//   - "Corpo" = [u8 tipo][campos...], sempre em byte order de rede (big-endian).
//
// As mesmas funcoes serializar/parse alimentam os dois transportes.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "comum/tipos.hpp"

namespace rede {

using Buffer = std::vector<std::uint8_t>;

// ---------------------------------------------------------------------------
// Tipos de mensagem, separados em faixas por canal. Um leitor de canal de
// NEGOCIO nunca deve ver um tipo de CONTROLE/TELEMETRIA, e vice-versa; as
// faixas distintas permitem afirmar isso e capturar bugs.
// ---------------------------------------------------------------------------
enum class TipoMsg : std::uint8_t {
    // --- Negocio (TCP peer-to-peer entre workers) ----------- faixa 0x0_
    TAREFAS       = 0x01,  // lote de tarefas (estado de canal no snapshot)
    MARCADOR      = 0x02,  // marcador Chandy-Lamport (delimitador, nao e estado)
    PEDIDO_ROUBO  = 0x03,  // pedido de trabalho (estado de canal no snapshot)
    OLA           = 0x04,  // handshake: o lado que conecta anuncia seu NodeId;
                           // consumida no setup do canal, FORA do snapshot

    // --- Controle (TCP Monitor <-> worker) ------------------- faixa 0x1_
    INICIAR_SNAPSHOT = 0x10,  // Monitor -> worker iniciador
    PECA_SNAPSHOT    = 0x11,  // worker  -> Monitor (captura local serializada)

    // --- Telemetria (UDP worker -> Monitor; fire-and-forget) - faixa 0x2_
    TELE_ESTADO = 0x20,  // estado periodico (alimenta as barras)
    TELE_EVENTO = 0x21,  // evento de log (alimenta a tabela de logs)
};

// Eventos reportados via TELE_EVENTO. O Monitor mapeia para texto; nao
// trafegamos strings de tamanho variavel por UDP.
enum class Evento : std::uint8_t {
    ENVIOU_LOTE        = 1,
    RECEBEU_LOTE       = 2,
    PEDIU_ROUBO        = 3,
    INICIOU_SNAPSHOT   = 4,
    RECEBEU_MARCADOR   = 5,
    GRAVOU_CANAL       = 6,
    CONCLUIU_SNAPSHOT  = 7,
};

// ---------------------------------------------------------------------------
// Mensagens de negocio
// ---------------------------------------------------------------------------
struct MsgTarefas {
    comum::Contagem quantidade;
};

struct MsgMarcador {
    comum::SnapshotId snapshot_id;
};

struct MsgPedidoRoubo {
    comum::Contagem quantidade_sugerida;  // 0 = "voce decide"
};

// Handshake de canal: enviada UMA vez logo apos conectar. Como accept() so
// revela a porta efemera do cliente (nao a identidade do vizinho), o lado que
// conecta anuncia aqui seu NodeId para o lado que aceita mapear a conexao ao
// vizinho correto. O canal a consome no setup e nunca a repassa ao snapshot.
struct MsgOla {
    comum::NodeId remetente;
};

// ---------------------------------------------------------------------------
// Mensagens de controle
// ---------------------------------------------------------------------------
struct MsgIniciarSnapshot {
    comum::SnapshotId snapshot_id;  // atribuido pelo Monitor (que agrega)
};

// RESERVADA. Na coleta atual (via filesystem) o worker publica a peca em arquivo
// e o Monitor a le da pasta; esta mensagem fica definida para a alternativa
// "Monitor como coletor de rede" (execucao multi-maquina, sem FS compartilhado).
struct MsgPecaSnapshot {
    comum::SnapshotId snapshot_id;
    comum::NodeId     worker;
    std::string       json;  // captura local serializada (estado_capturado)
};

// ---------------------------------------------------------------------------
// Mensagens de telemetria (UDP)
// ---------------------------------------------------------------------------
struct MsgTeleEstado {
    comum::NodeId   worker;
    std::uint16_t   fator;        // fator de clock (heterogeneidade)
    comum::Contagem pendentes;
    comum::Contagem concluidas;
    std::uint8_t    em_snapshot;  // 0/1: esta gravando um snapshot agora
    std::uint32_t   seq;          // descarta pacotes UDP fora de ordem
};

struct MsgTeleEvento {
    comum::NodeId     worker;
    Evento            evento;
    comum::NodeId     peer;         // vizinho envolvido (0 se nao se aplica)
    comum::Contagem   valor;        // quantidade de tarefas (se aplica)
    comum::SnapshotId snapshot_id;  // se for evento de snapshot
    std::uint32_t     seq;
};

// ---------------------------------------------------------------------------
// Serializacao: cada funcao devolve o CORPO [u8 tipo][campos], sem prefixo.
// ---------------------------------------------------------------------------
Buffer serializar(const MsgTarefas&);
Buffer serializar(const MsgMarcador&);
Buffer serializar(const MsgPedidoRoubo&);
Buffer serializar(const MsgOla&);
Buffer serializar(const MsgIniciarSnapshot&);
Buffer serializar(const MsgPecaSnapshot&);
Buffer serializar(const MsgTeleEstado&);
Buffer serializar(const MsgTeleEvento&);

// Le o tipo de um corpo (primeiro byte). std::nullopt se vazio.
std::optional<TipoMsg> tipo_de(const Buffer& corpo);

// Parse: validam tamanho/conteudo e devolvem std::nullopt se malformado.
std::optional<MsgTarefas>        parse_tarefas(const Buffer& corpo);
std::optional<MsgMarcador>       parse_marcador(const Buffer& corpo);
std::optional<MsgPedidoRoubo>    parse_pedido_roubo(const Buffer& corpo);
std::optional<MsgOla>            parse_ola(const Buffer& corpo);
std::optional<MsgIniciarSnapshot> parse_iniciar_snapshot(const Buffer& corpo);
std::optional<MsgPecaSnapshot>   parse_peca_snapshot(const Buffer& corpo);
std::optional<MsgTeleEstado>     parse_tele_estado(const Buffer& corpo);
std::optional<MsgTeleEvento>     parse_tele_evento(const Buffer& corpo);

// ---------------------------------------------------------------------------
// Enquadramento TCP. (UDP nao usa: o datagrama ja delimita a mensagem.)
// ---------------------------------------------------------------------------
constexpr std::size_t TAM_PREFIXO = 4;  // u32 big-endian com o tamanho do corpo

// Devolve [u32 tamanho][corpo], pronto para um unico write() no socket TCP.
Buffer quadro_tcp(const Buffer& corpo);

// Le o prefixo de tamanho de 4 bytes (usado pelo leitor de frames do socket).
std::uint32_t ler_prefixo(const std::uint8_t* quatro_bytes);

}  // namespace rede
