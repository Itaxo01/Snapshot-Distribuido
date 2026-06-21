// Implementacao do Worker (laco consumidor e fiacao das camadas).
#include "entidade/worker.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

#include "comum/json.hpp"
#include "comum/parametros.hpp"

namespace entidade {
namespace {

using ms = std::chrono::milliseconds;

// Atalhos para os parametros centrais (ver comum/parametros.hpp).
constexpr int    TICK_MS           = comum::parametro::WORKER_TICK_MS;
constexpr int    INTERVALO_DIST_MS = comum::parametro::WORKER_INTERVALO_DIST_MS;
constexpr int    INTERVALO_TELE_MS = comum::parametro::WORKER_INTERVALO_TELE_MS;
constexpr double TAXA_BASE         = comum::parametro::TAREFAS_POR_MS_POR_FATOR;

}  // namespace

std::vector<comum::NodeId> Worker::ids_de(const std::vector<comum::Vizinho>& vs) {
    std::vector<comum::NodeId> r;
    r.reserve(vs.size());
    for (const auto& v : vs) r.push_back(v.porta);
    return r;
}

Worker::Worker(comum::ConfigWorker cfg)
    : cfg_(cfg),
      params_(),
      engine_(cfg.id(), ids_de(cfg.vizinhos), ids_de(cfg.vizinhos),
              [this] { return capturar_local(); },
              [this](comum::NodeId d, comum::SnapshotId id) { enviar_marcador(d, id); },
              [this](const snapshot::PecaLocal& p) { ao_completar(p); }),
      controle_(comum::Endereco{cfg.escuta.host, cfg.porta_controle}),
      telemetria_(cfg.monitor_telemetria) {
    estado_.pendentes = cfg.tarefas_iniciais;
}

// --- Bootstrap --------------------------------------------------------------
void Worker::preparar() {
    controle_.iniciar();
    servidor_negocio_ = rede::escutar_tcp(cfg_.escuta);

    // Regra "menor id conecta no maior": conecta nos vizinhos de id MAIOR e
    // aceita dos de id MENOR. O backlog de escuta absorve quem chega durante a
    // fase de conexao, evitando deadlock sem threads extras.
    int aceitar_n = 0;
    for (const auto& v : cfg_.vizinhos) {
        if (v.porta > cfg_.id())
            registrar_canal(rede::Canal::conectar(cfg_.id(), v, inbox_));
        else
            ++aceitar_n;
    }
    for (int k = 0; k < aceitar_n; ++k) {
        rede::Socket s = rede::aceitar(servidor_negocio_);
        registrar_canal(rede::Canal::aceitar(std::move(s), inbox_));
    }

    auto agora = Relogio::now();
    ultimo_processamento_ = agora;
    prox_dist_ = agora + ms(INTERVALO_DIST_MS);
    prox_tele_ = agora + ms(INTERVALO_TELE_MS);
}

void Worker::registrar_canal(std::unique_ptr<rede::Canal> c) {
    comum::NodeId v = c->vizinho();
    canal_por_no_[v] = c.get();
    ids_vizinhos_.push_back(v);
    canais_.push_back(std::move(c));
}

// --- Laco consumidor --------------------------------------------------------
void Worker::executar() {
    while (rodando_.load(std::memory_order_relaxed)) {
        drenar_comandos();
        if (auto msg = inbox_.pop_por(ms(TICK_MS)))
            despachar(*msg);
        processar_tarefas();

        auto agora = Relogio::now();
        if (agora >= prox_dist_) { distribuir();    prox_dist_ = agora + ms(INTERVALO_DIST_MS); }
        if (agora >= prox_tele_) { emitir_estado(); prox_tele_ = agora + ms(INTERVALO_TELE_MS); }
    }
    // encerra recursos de rede na propria thread consumidora (sem concorrencia)
    for (auto& c : canais_) c->parar();
    controle_.parar();
}

void Worker::parar() { rodando_.store(false, std::memory_order_relaxed); }

// --- Passos do laco ---------------------------------------------------------
void Worker::drenar_comandos() {
    while (auto cmd = controle_.comandos().pop_por(ms(0))) {
        if (cmd->tipo == Comando::Tipo::INICIAR_SNAPSHOT) {
            engine_.iniciar(cmd->snapshot_id);
            evento(rede::Evento::INICIOU_SNAPSHOT, 0, 0, cmd->snapshot_id);
        }
    }
}

void Worker::despachar(const rede::MensagemRecebida& msg) {
    auto t = rede::tipo_de(msg.corpo);
    if (!t) return;
    switch (*t) {
        case rede::TipoMsg::MARCADOR:
            if (auto m = rede::parse_marcador(msg.corpo)) {
                engine_.ao_receber_marcador(msg.origem, m->snapshot_id);
                evento(rede::Evento::RECEBEU_MARCADOR, msg.origem, 0, m->snapshot_id);
            }
            break;

        case rede::TipoMsg::TAREFAS:
            engine_.ao_receber_negocio(msg.origem, msg.corpo);  // registra p/ snapshot
            if (auto m = rede::parse_tarefas(msg.corpo)) {
                estado_.pendentes += m->quantidade;             // incorpora (vivo)
                evento(rede::Evento::RECEBEU_LOTE, msg.origem, m->quantidade, 0);
            }
            break;

        case rede::TipoMsg::PEDIDO_ROUBO: {
            engine_.ao_receber_negocio(msg.origem, msg.corpo);
            comum::Contagem sug = 0;
            if (auto m = rede::parse_pedido_roubo(msg.corpo)) sug = m->quantidade_sugerida;
            comum::Contagem lote = lote_para_doar(estado_.pendentes, sug, params_);
            if (lote > 0) {
                estado_.pendentes -= lote;
                enviar_para(msg.origem, rede::serializar(rede::MsgTarefas{lote}));
                evento(rede::Evento::ENVIOU_LOTE, msg.origem, lote, 0);
            }
            break;
        }
        default:
            break;  // OLA fora de hora ou tipo inesperado: ignora
    }
}

void Worker::processar_tarefas() {
    auto agora = Relogio::now();
    double dt = std::chrono::duration<double, std::milli>(agora - ultimo_processamento_).count();
    ultimo_processamento_ = agora;

    auto n = static_cast<comum::Contagem>(dt * cfg_.fator * TAXA_BASE);
    if (n == 0) return;
    if (n > estado_.pendentes) n = estado_.pendentes;
    estado_.pendentes  -= n;
    estado_.concluidas += n;
}

void Worker::distribuir() {
    if (ids_vizinhos_.empty()) return;
    comum::Contagem lote = lote_para_empurrar(estado_.pendentes, params_);
    if (lote > 0) {
        comum::NodeId destino = vizinho_aleatorio();
        estado_.pendentes -= lote;
        enviar_para(destino, rede::serializar(rede::MsgTarefas{lote}));
        evento(rede::Evento::ENVIOU_LOTE, destino, lote, 0);
    } else if (deve_pedir(estado_.pendentes, params_)) {
        comum::NodeId destino = vizinho_aleatorio();
        enviar_para(destino, rede::serializar(rede::MsgPedidoRoubo{0}));
        evento(rede::Evento::PEDIU_ROUBO, destino, 0, 0);
    }
}

void Worker::emitir_estado() {
    telemetria_.enviar(rede::MsgTeleEstado{
        cfg_.id(), cfg_.fator, estado_.pendentes, estado_.concluidas,
        static_cast<std::uint8_t>(engine_.em_andamento() ? 1 : 0), ++seq_estado_});
}

// --- Callbacks do snapshot --------------------------------------------------
rede::Buffer Worker::capturar_local() { return estado_.codificar(); }

void Worker::enviar_marcador(comum::NodeId destino, comum::SnapshotId id) {
    enviar_para(destino, rede::serializar(rede::MsgMarcador{id}));
}

void Worker::ao_completar(const snapshot::PecaLocal& p) {
    EstadoNegocio e = EstadoNegocio::decodificar(p.estado_local);

    std::vector<std::string> canais;
    for (const auto& [origem, msgs] : p.canais) {
        comum::Contagem tarefas = 0;
        for (const auto& b : msgs)
            if (auto t = rede::parse_tarefas(b)) tarefas += t->quantidade;
        canais.push_back(comum::json::Escritor()
                             .numero("origem", origem)
                             .numero("mensagens", static_cast<long long>(msgs.size()))
                             .numero("tarefas", tarefas)
                             .str());
    }

    std::string doc = comum::json::Escritor()
                          .numero_u("snapshot_id", p.id)
                          .numero("worker", p.no)
                          .numero("pendentes", e.pendentes)
                          .numero("concluidas", e.concluidas)
                          .bruto("canais", comum::json::array(canais))
                          .str();

    salvar_peca_local(p.id, p.no, doc);  // publica em arquivo; o Monitor coleta via FS
    evento(rede::Evento::CONCLUIU_SNAPSHOT, 0, 0, p.id);
}

// --- Auxiliares -------------------------------------------------------------
void Worker::enviar_para(comum::NodeId destino, const rede::Buffer& corpo) {
    auto it = canal_por_no_.find(destino);
    if (it == canal_por_no_.end()) return;
    try {
        it->second->enviar(corpo);
    } catch (const rede::ErroRede&) {
        // conexao caiu: o projeto nao trata falhas; descarta o envio.
    }
}

comum::NodeId Worker::vizinho_aleatorio() {
    std::uniform_int_distribution<std::size_t> d(0, ids_vizinhos_.size() - 1);
    return ids_vizinhos_[d(rng_)];
}

void Worker::evento(rede::Evento ev, comum::NodeId peer, comum::Contagem valor,
                    comum::SnapshotId sid) {
    telemetria_.enviar(rede::MsgTeleEvento{cfg_.id(), ev, peer, valor, sid, ++seq_ev_});
}

void Worker::salvar_peca_local(comum::SnapshotId id, comum::NodeId no,
                               const std::string& json) {
    try {
        std::string dir   = comum::pasta_snapshot(id);
        std::filesystem::create_directories(dir);
        std::string final = dir + "/" + std::to_string(no) + ".json";
        std::string tmp   = final + ".tmp";
        // Publicacao ATOMICA: grava no .tmp, fecha, e renomeia. Assim o Monitor
        // (que varre a pasta) nunca le um arquivo pela metade.
        { std::ofstream f(tmp); f << json; }
        std::filesystem::rename(tmp, final);
    } catch (...) {
        // I/O nao pode derrubar o worker.
    }
}

}  // namespace entidade
