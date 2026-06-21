// Implementacao do receptor de telemetria e do estado do cluster.
#include "monitor/receptor_telemetria.hpp"

#include <chrono>

#include "comum/parametros.hpp"

namespace monitor {
namespace {

constexpr std::size_t MAX_LOG = comum::parametro::MONITOR_MAX_LOG;

// Traduz um evento de telemetria em uma linha de log legivel.
std::string formatar_evento(const rede::MsgTeleEvento& e) {
    std::string w   = std::to_string(e.worker);
    std::string p   = std::to_string(e.peer);
    std::string v   = std::to_string(e.valor);
    std::string sid = std::to_string(comum::snapshot_seq(e.snapshot_id));
    switch (e.evento) {
        case rede::Evento::ENVIOU_LOTE:       return w + " -> " + p + " enviou " + v;
        case rede::Evento::RECEBEU_LOTE:      return w + " recebeu " + v + " de " + p;
        case rede::Evento::PEDIU_ROUBO:       return w + " pediu trabalho a " + p;
        case rede::Evento::INICIOU_SNAPSHOT:  return w + " iniciou snapshot #" + sid;
        case rede::Evento::RECEBEU_MARCADOR:  return w + " recebeu marcador de " + p + " (#" + sid + ")";
        case rede::Evento::GRAVOU_CANAL:      return w + " gravou canal de " + p;
        case rede::Evento::CONCLUIU_SNAPSHOT: return w + " concluiu snapshot #" + sid;
    }
    return w + " evento desconhecido";
}

}  // namespace

void EstadoCluster::aplicar_estado(const rede::MsgTeleEstado& m) {
    std::lock_guard<std::mutex> lk(m_);
    EstadoWorker& w = workers_[m.worker];
    if (w.id != 0 && m.seq < w.ultimo_seq) return;  // pacote atrasado: ignora
    w.id          = m.worker;
    w.fator       = m.fator;
    w.pendentes   = m.pendentes;
    w.concluidas  = m.concluidas;
    w.em_snapshot = (m.em_snapshot != 0);
    w.ultimo_seq  = m.seq;
}

void EstadoCluster::aplicar_evento(std::string linha) {
    std::lock_guard<std::mutex> lk(m_);
    log_.push_back(std::move(linha));
    if (log_.size() > MAX_LOG) log_.pop_front();
}

std::map<comum::NodeId, EstadoWorker> EstadoCluster::copia_workers() const {
    std::lock_guard<std::mutex> lk(m_);
    return workers_;
}

std::vector<std::string> EstadoCluster::copia_log() const {
    std::lock_guard<std::mutex> lk(m_);
    return {log_.begin(), log_.end()};
}

ReceptorTelemetria::ReceptorTelemetria(comum::Porta porta, EstadoCluster& estado)
    : receptor_(porta), estado_(estado) {
    receptor_.definir_timeout(
        std::chrono::milliseconds(comum::parametro::MONITOR_TELE_TIMEOUT_MS));  // p/ checar parado_
}

ReceptorTelemetria::~ReceptorTelemetria() { parar(); }

void ReceptorTelemetria::iniciar() {
    th_ = std::thread([this] { laco(); });
}

void ReceptorTelemetria::parar() {
    if (parado_.exchange(true)) return;
    if (th_.joinable()) th_.join();
}

void ReceptorTelemetria::laco() {
    while (!parado_.load(std::memory_order_relaxed)) {
        auto corpo = receptor_.receber();
        if (!corpo) continue;  // timeout
        auto t = rede::tipo_de(*corpo);
        if (!t) continue;
        if (*t == rede::TipoMsg::TELE_ESTADO) {
            if (auto m = rede::parse_tele_estado(*corpo)) estado_.aplicar_estado(*m);
        } else if (*t == rede::TipoMsg::TELE_EVENTO) {
            if (auto m = rede::parse_tele_evento(*corpo))
                estado_.aplicar_evento(formatar_evento(*m));
        }
    }
}

}  // namespace monitor
