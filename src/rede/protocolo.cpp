// Implementacao do protocolo de fio: serializacao/parse em byte order de rede.
#include "rede/protocolo.hpp"

#include <arpa/inet.h>  // htonl / ntohl
#include <endian.h>     // htobe64 / be64toh

#include <cstring>      // memcpy

namespace rede {
namespace {

// --- Escrita: anexa inteiros big-endian ao fim do buffer --------------------
void por_u8(Buffer& b, std::uint8_t v) { b.push_back(v); }

void por_u16(Buffer& b, std::uint16_t v) {
    std::uint16_t n = htons(v);
    auto* p = reinterpret_cast<const std::uint8_t*>(&n);
    b.insert(b.end(), p, p + sizeof(n));
}

void por_u32(Buffer& b, std::uint32_t v) {
    std::uint32_t n = htonl(v);
    auto* p = reinterpret_cast<const std::uint8_t*>(&n);
    b.insert(b.end(), p, p + sizeof(n));
}

void por_u64(Buffer& b, std::uint64_t v) {
    std::uint64_t n = htobe64(v);
    auto* p = reinterpret_cast<const std::uint8_t*>(&n);
    b.insert(b.end(), p, p + sizeof(n));
}

// --- Leitura: cursor com checagem de limites --------------------------------
// Em qualquer leitura fora dos limites, marca 'ok=false' e nao avanca; o
// chamador checa 'ok' uma vez no fim e devolve std::nullopt se falhou.
struct Cursor {
    const Buffer& b;
    std::size_t   pos = 0;
    bool          ok  = true;

    explicit Cursor(const Buffer& buf) : b(buf) {}

    bool cabe(std::size_t n) const { return pos + n <= b.size(); }

    std::uint8_t u8() {
        if (!ok || !cabe(1)) { ok = false; return 0; }
        return b[pos++];
    }
    std::uint16_t u16() {
        if (!ok || !cabe(2)) { ok = false; return 0; }
        std::uint16_t n;
        std::memcpy(&n, b.data() + pos, sizeof(n));
        pos += sizeof(n);
        return ntohs(n);
    }
    std::uint32_t u32() {
        if (!ok || !cabe(4)) { ok = false; return 0; }
        std::uint32_t n;
        std::memcpy(&n, b.data() + pos, sizeof(n));
        pos += sizeof(n);
        return ntohl(n);
    }
    std::uint64_t u64() {
        if (!ok || !cabe(8)) { ok = false; return 0; }
        std::uint64_t n;
        std::memcpy(&n, b.data() + pos, sizeof(n));
        pos += sizeof(n);
        return be64toh(n);
    }
    std::string resto() {  // consome o que sobrou como bytes crus
        if (!ok) return {};
        std::string s(b.begin() + pos, b.end());
        pos = b.size();
        return s;
    }
    bool fim() const { return pos == b.size(); }
};

Buffer corpo_com_tipo(TipoMsg t) {
    Buffer b;
    por_u8(b, static_cast<std::uint8_t>(t));
    return b;
}

}  // namespace

// ===========================================================================
// Serializacao
// ===========================================================================
Buffer serializar(const MsgTarefas& m) {
    Buffer b = corpo_com_tipo(TipoMsg::TAREFAS);
    por_u32(b, m.quantidade);
    return b;
}

Buffer serializar(const MsgMarcador& m) {
    Buffer b = corpo_com_tipo(TipoMsg::MARCADOR);
    por_u64(b, m.snapshot_id);
    return b;
}

Buffer serializar(const MsgPedidoRoubo& m) {
    Buffer b = corpo_com_tipo(TipoMsg::PEDIDO_ROUBO);
    por_u32(b, m.quantidade_sugerida);
    return b;
}

Buffer serializar(const MsgOla& m) {
    Buffer b = corpo_com_tipo(TipoMsg::OLA);
    por_u16(b, m.remetente);
    return b;
}

Buffer serializar(const MsgIniciarSnapshot& m) {
    Buffer b = corpo_com_tipo(TipoMsg::INICIAR_SNAPSHOT);
    por_u64(b, m.snapshot_id);
    return b;
}

Buffer serializar(const MsgPecaSnapshot& m) {
    Buffer b = corpo_com_tipo(TipoMsg::PECA_SNAPSHOT);
    por_u64(b, m.snapshot_id);
    por_u16(b, m.worker);
    por_u32(b, static_cast<std::uint32_t>(m.json.size()));
    b.insert(b.end(), m.json.begin(), m.json.end());
    return b;
}

Buffer serializar(const MsgTeleEstado& m) {
    Buffer b = corpo_com_tipo(TipoMsg::TELE_ESTADO);
    por_u16(b, m.worker);
    por_u16(b, m.fator);
    por_u32(b, m.pendentes);
    por_u32(b, m.concluidas);
    por_u8(b, m.em_snapshot);
    por_u32(b, m.seq);
    return b;
}

Buffer serializar(const MsgTeleEvento& m) {
    Buffer b = corpo_com_tipo(TipoMsg::TELE_EVENTO);
    por_u16(b, m.worker);
    por_u8(b, static_cast<std::uint8_t>(m.evento));
    por_u16(b, m.peer);
    por_u32(b, m.valor);
    por_u64(b, m.snapshot_id);
    por_u32(b, m.seq);
    return b;
}

// ===========================================================================
// Parse
// ===========================================================================
std::optional<TipoMsg> tipo_de(const Buffer& corpo) {
    if (corpo.empty()) return std::nullopt;
    return static_cast<TipoMsg>(corpo[0]);
}

std::optional<MsgTarefas> parse_tarefas(const Buffer& corpo) {
    Cursor c(corpo);
    c.u8();  // tipo
    MsgTarefas m{};
    m.quantidade = c.u32();
    if (!c.ok || !c.fim()) return std::nullopt;
    return m;
}

std::optional<MsgMarcador> parse_marcador(const Buffer& corpo) {
    Cursor c(corpo);
    c.u8();
    MsgMarcador m{};
    m.snapshot_id = c.u64();
    if (!c.ok || !c.fim()) return std::nullopt;
    return m;
}

std::optional<MsgPedidoRoubo> parse_pedido_roubo(const Buffer& corpo) {
    Cursor c(corpo);
    c.u8();
    MsgPedidoRoubo m{};
    m.quantidade_sugerida = c.u32();
    if (!c.ok || !c.fim()) return std::nullopt;
    return m;
}

std::optional<MsgOla> parse_ola(const Buffer& corpo) {
    Cursor c(corpo);
    c.u8();
    MsgOla m{};
    m.remetente = c.u16();
    if (!c.ok || !c.fim()) return std::nullopt;
    return m;
}

std::optional<MsgIniciarSnapshot> parse_iniciar_snapshot(const Buffer& corpo) {
    Cursor c(corpo);
    c.u8();
    MsgIniciarSnapshot m{};
    m.snapshot_id = c.u64();
    if (!c.ok || !c.fim()) return std::nullopt;
    return m;
}

std::optional<MsgPecaSnapshot> parse_peca_snapshot(const Buffer& corpo) {
    Cursor c(corpo);
    c.u8();
    MsgPecaSnapshot m{};
    m.snapshot_id = c.u64();
    m.worker      = c.u16();
    std::uint32_t tam = c.u32();
    if (!c.ok) return std::nullopt;
    // O JSON deve ocupar exatamente o restante do corpo.
    if (corpo.size() - c.pos != tam) return std::nullopt;
    m.json = c.resto();
    return m;
}

std::optional<MsgTeleEstado> parse_tele_estado(const Buffer& corpo) {
    Cursor c(corpo);
    c.u8();
    MsgTeleEstado m{};
    m.worker      = c.u16();
    m.fator       = c.u16();
    m.pendentes   = c.u32();
    m.concluidas  = c.u32();
    m.em_snapshot = c.u8();
    m.seq         = c.u32();
    if (!c.ok || !c.fim()) return std::nullopt;
    return m;
}

std::optional<MsgTeleEvento> parse_tele_evento(const Buffer& corpo) {
    Cursor c(corpo);
    c.u8();
    MsgTeleEvento m{};
    m.worker      = c.u16();
    m.evento      = static_cast<Evento>(c.u8());
    m.peer        = c.u16();
    m.valor       = c.u32();
    m.snapshot_id = c.u64();
    m.seq         = c.u32();
    if (!c.ok || !c.fim()) return std::nullopt;
    return m;
}

// ===========================================================================
// Enquadramento TCP
// ===========================================================================
Buffer quadro_tcp(const Buffer& corpo) {
    Buffer q;
    q.reserve(TAM_PREFIXO + corpo.size());
    por_u32(q, static_cast<std::uint32_t>(corpo.size()));
    q.insert(q.end(), corpo.begin(), corpo.end());
    return q;
}

std::uint32_t ler_prefixo(const std::uint8_t* quatro_bytes) {
    std::uint32_t n;
    std::memcpy(&n, quatro_bytes, sizeof(n));
    return ntohl(n);
}

}  // namespace rede
