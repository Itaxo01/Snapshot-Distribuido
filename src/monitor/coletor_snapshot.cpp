// Implementacao do coletor de snapshots via filesystem.
#include "monitor/coletor_snapshot.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "comum/json.hpp"
#include "comum/parametros.hpp"
#include "rede/protocolo.hpp"

namespace monitor {
namespace fs = std::filesystem;
namespace {

constexpr int INTERVALO_VARREDURA_MS = comum::parametro::MONITOR_VARREDURA_MS;

std::string ler_arquivo(const std::string& caminho) {
    std::ifstream f(caminho);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Le snapshots/<id>/<no>.json -> PecaColetada. 'ok' = arquivo valido e completo.
PecaColetada ler_peca(const std::string& caminho, bool& ok) {
    PecaColetada pc;
    ok = false;
    auto v = comum::json::parse(ler_arquivo(caminho));
    if (!v) return pc;
    if (auto* x = v->campo("worker"))     pc.no         = static_cast<comum::NodeId>(x->inteiro());
    if (auto* x = v->campo("pendentes"))  pc.pendentes  = static_cast<comum::Contagem>(x->inteiro());
    if (auto* x = v->campo("concluidas")) pc.concluidas = static_cast<comum::Contagem>(x->inteiro());
    if (auto* c = v->campo("canais"))
        for (auto& ch : c->arr)
            if (auto* x = ch.campo("tarefas"))
                pc.em_transito += static_cast<comum::Contagem>(x->inteiro());
    ok = true;
    return pc;
}

}  // namespace

Coletor::Coletor(std::vector<EndpointCtrl> workers, comum::Contagem total)
    : workers_(std::move(workers)), total_(total) {
    // Semeia o contador de seq com o tempo atual (epoch em segundos) para que os
    // ids desta sessao nao colidam com pastas de execucoes anteriores -- a parte
    // alta do SnapshotId e o iniciador, entao so a seq precisa ser unica no tempo.
    seq_ = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

Coletor::~Coletor() { parar(); }

void Coletor::conectar() {
    conns_.clear();
    for (const auto& w : workers_)
        conns_.push_back(rede::conectar_tcp(w.controle, comum::parametro::CONEXAO_TENTATIVAS,
                                            comum::parametro::CONEXAO_ESPERA_MS));
}

void Coletor::iniciar() {
    th_ = std::thread([this] {
        while (!parado_.load(std::memory_order_relaxed)) {
            varrer_uma_vez();
            std::this_thread::sleep_for(std::chrono::milliseconds(INTERVALO_VARREDURA_MS));
        }
    });
}

void Coletor::parar() {
    if (parado_.exchange(true)) return;
    if (th_.joinable()) th_.join();
}

int Coletor::indice_de(comum::NodeId id) const {
    for (std::size_t i = 0; i < workers_.size(); ++i)
        if (workers_[i].id == id) return static_cast<int>(i);
    return -1;
}

comum::SnapshotId Coletor::disparar(comum::NodeId iniciador) {
    int idx = indice_de(iniciador);
    if (idx < 0) return 0;

    comum::SnapshotId sid;
    {
        std::lock_guard<std::mutex> lk(m_);
        sid = comum::fazer_snapshot_id(iniciador, ++seq_);
        disparados_.insert(sid);
    }
    try {
        rede::enviar_frame(conns_[idx].fd(), rede::serializar(rede::MsgIniciarSnapshot{sid}));
    } catch (const rede::ErroRede&) {
        // sem o INICIAR o snapshot nao acontece; a pasta nunca completa e o
        // resultado simplesmente nao aparece. O projeto nao trata falhas.
    }
    return sid;
}

// Agrega snapshots/<sid>/: exige UMA peca por worker conhecido. false se faltar
// alguma (snapshot ainda em andamento, ou pasta de outro cluster).
bool Coletor::agregar(comum::SnapshotId sid, ResultadoSnapshot& res) const {
    std::string dir = comum::pasta_snapshot(sid);
    res = ResultadoSnapshot{};
    res.id        = sid;
    res.iniciador = comum::snapshot_iniciador(sid);
    res.total     = static_cast<long long>(total_);

    for (const auto& w : workers_) {
        std::string caminho = dir + "/" + std::to_string(w.id) + ".json";
        std::error_code ec;
        if (!fs::exists(caminho, ec)) return false;  // peca ainda nao publicada
        bool ok = false;
        PecaColetada pc = ler_peca(caminho, ok);
        if (!ok) return false;  // arquivo a meio caminho (sem .tmp->rename): tenta depois
        pc.no = w.id;
        res.soma             += pc.pendentes + pc.concluidas + pc.em_transito;
        res.pendentes_totais += pc.pendentes;
        res.transito_totais  += pc.em_transito;
        res.pecas.push_back(pc);
    }
    res.consistente = (res.soma == res.total);
    // Termino: nada pendente em lugar nenhum -- nem local nem em transito.
    res.terminado = res.consistente && res.pendentes_totais == 0 &&
                    res.transito_totais == 0;
    return true;
}

void Coletor::varrer_uma_vez() {
    std::error_code ec;
    if (!fs::exists("snapshots", ec)) return;

    for (const auto& ent : fs::directory_iterator("snapshots", ec)) {
        if (ec) break;
        if (!ent.is_directory()) continue;

        comum::SnapshotId sid;
        try {
            sid = std::stoull(ent.path().filename().string());
        } catch (...) {
            continue;  // nome de pasta nao numerico
        }

        {  // ja agregado?
            std::lock_guard<std::mutex> lk(m_);
            if (prontos_.count(sid)) continue;
        }

        ResultadoSnapshot res;
        if (!agregar(sid, res)) continue;  // incompleto: tenta na proxima volta

        {  // marca se foi disparado nesta sessao
            std::lock_guard<std::mutex> lk(m_);
            res.sessao = disparados_.count(sid) != 0;
        }

        // persiste o snapshot global agregado
        try {
            std::vector<std::string> arr;
            for (const auto& p : res.pecas)
                arr.push_back(comum::json::Escritor()
                                  .numero("id", p.no)
                                  .numero("pendentes", p.pendentes)
                                  .numero("concluidas", p.concluidas)
                                  .numero("em_transito", p.em_transito)
                                  .str());
            std::string doc = comum::json::Escritor()
                                  .numero_u("snapshot_id", sid)
                                  .numero("iniciador", res.iniciador)
                                  .numero("total", res.total)
                                  .numero("soma", res.soma)
                                  .bruto("consistente", res.consistente ? "true" : "false")
                                  .numero("pendentes_totais", res.pendentes_totais)
                                  .numero("transito_totais", res.transito_totais)
                                  .bruto("terminado", res.terminado ? "true" : "false")
                                  .bruto("workers", comum::json::array(arr))
                                  .str();
            res.caminho = comum::pasta_snapshot(sid) + "/final_snapshot.json";
            std::ofstream(res.caminho) << doc << "\n";
        } catch (...) {
            // falha de I/O nao invalida o resultado em memoria
        }

        std::lock_guard<std::mutex> lk(m_);
        prontos_[sid] = std::move(res);
    }
}

std::vector<ResultadoSnapshot> Coletor::resultados_sessao() const {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<ResultadoSnapshot> r;
    for (auto it = prontos_.rbegin(); it != prontos_.rend(); ++it)  // recentes primeiro
        if (it->second.sessao) r.push_back(it->second);
    return r;
}

std::vector<ResultadoSnapshot> Coletor::resultados_anteriores() const {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<ResultadoSnapshot> r;
    for (auto it = prontos_.rbegin(); it != prontos_.rend(); ++it)
        if (!it->second.sessao) r.push_back(it->second);
    return r;
}

}  // namespace monitor
