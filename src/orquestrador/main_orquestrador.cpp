// Entry point do orquestrador: ponto unico de inicializacao da aplicacao.
// Constroi a topologia, atribui portas/identidades, distribui a carga e
// lanca (fork/exec) um processo entidade por no. Encaminha o sinal de
// encerramento aos filhos.
//
// Uso:
//   orquestrador --workers N [--tarefas 100000] [--topologia ring|mesh]
//                [--porta-base 7001] [--controle-base 8001] [--telemetria 9000]
//                [--host 127.0.0.1] [--distribuir] [--entidade <caminho>]
#include <linux/limits.h>  // PATH_MAX
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "comum/json.hpp"
#include "comum/tipos.hpp"
#include "orquestrador/spawner.hpp"
#include "orquestrador/topologia.hpp"

namespace {

constexpr int MAX_WORKERS = 256;

// pids dos filhos, acessiveis pelo handler de sinal (kill e async-signal-safe).
pid_t            g_pids[MAX_WORKERS];
std::atomic<int> g_npids{0};

void encerrar_filhos(int) {
    int n = g_npids.load();
    for (int i = 0; i < n; ++i)
        if (g_pids[i] > 0) ::kill(g_pids[i], SIGTERM);
}

[[noreturn]] void uso(const char* prog) {
    std::cerr << "uso: " << prog
              << " --workers N [--tarefas 100000] [--topologia ring|mesh]"
                 " [--porta-base 7001] [--controle-base 8001] [--telemetria 9000]"
                 " [--host 127.0.0.1] [--distribuir] [--entidade <caminho>]\n";
    std::exit(2);
}

std::string dir_do_executavel() {
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    std::string p(buf);
    auto pos = p.rfind('/');
    return pos == std::string::npos ? "." : p.substr(0, pos);
}

struct Opcoes {
    int                     workers       = 0;
    comum::Contagem         tarefas       = 100000;
    orquestrador::Topologia topologia     = orquestrador::Topologia::Anel;
    comum::Porta            porta_base    = 7001; // Porta worker <-> worker
    comum::Porta            controle_base = 8001; // Porta controle -> worker
    comum::Porta            telemetria    = 9000;
    std::string             host          = "127.0.0.1";
    bool                    distribuir    = false;
    std::string             entidade;  // vazio = auto (mesmo dir do orquestrador)
};

Opcoes parse(int argc, char** argv) {
    Opcoes o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto prox = [&]() -> std::string {
            if (i + 1 >= argc) uso(argv[0]);
            return argv[++i];
        };
        if (a == "--workers")             o.workers       = std::stoi(prox());
        else if (a == "--tarefas")        o.tarefas       = static_cast<comum::Contagem>(std::stoul(prox()));
        else if (a == "--topologia") {
            std::string t = prox();
            o.topologia = (t == "mesh" || t == "malha") ? orquestrador::Topologia::Malha
                                                        : orquestrador::Topologia::Anel;
        }
        else if (a == "--porta-base")     o.porta_base    = static_cast<comum::Porta>(std::stoi(prox()));
        else if (a == "--controle-base")  o.controle_base = static_cast<comum::Porta>(std::stoi(prox()));
        else if (a == "--telemetria")     o.telemetria    = static_cast<comum::Porta>(std::stoi(prox()));
        else if (a == "--host")           o.host          = prox();
        else if (a == "--distribuir")     o.distribuir    = true;
        else if (a == "--entidade")       o.entidade      = prox();
        else { std::cerr << "argumento desconhecido: " << a << "\n"; uso(argv[0]); }
    }
    if (o.workers < 1 || o.workers > MAX_WORKERS) uso(argv[0]);
    return o;
}

// Carga inicial de cada no.
std::vector<comum::Contagem> distribuir_tarefas(const Opcoes& o) {
    std::vector<comum::Contagem> t(o.workers, 0);
    if (!o.distribuir) {
        t[0] = o.tarefas;  // tudo no no 0 (espalhamento dramatico por work-stealing)
    } else {
        comum::Contagem base = o.tarefas / o.workers;
        comum::Contagem rem  = o.tarefas % o.workers;
        for (int i = 0; i < o.workers; ++i)
            t[i] = base + (static_cast<comum::Contagem>(i) < rem ? 1 : 0);
    }
    return t;
}

void escrever_cluster_json(const Opcoes& o, const std::vector<std::uint16_t>& fatores,
                           const std::vector<comum::Contagem>& tarefas) {
    std::vector<std::string> ws;
    for (int i = 0; i < o.workers; ++i) {
        ws.push_back(comum::json::Escritor()
                         .numero("id", o.porta_base + i)
                         .numero("controle", o.controle_base + i)
                         .numero("fator", fatores[i])
                         .numero("tarefas", tarefas[i])
                         .str());
    }
    std::string doc = comum::json::Escritor()
                          .texto("host", o.host)
                          .numero("telemetria", o.telemetria)
                          .bruto("workers", comum::json::array(ws))
                          .str();
    std::ofstream f("cluster.json");
    f << doc << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    Opcoes o = parse(argc, argv);
    if (o.workers < 3)
        std::cerr << "[aviso] o requisito pede >= 3 processos; usando "
                  << o.workers << "\n";

    std::string caminho_entidade =
        o.entidade.empty() ? dir_do_executavel() + "/entidade" : o.entidade;

    auto adj     = orquestrador::construir_topologia(o.workers, o.topologia);
    auto tarefas = distribuir_tarefas(o);
    std::vector<std::uint16_t> fatores(o.workers);
    for (int i = 0; i < o.workers; ++i)
        fatores[i] = static_cast<std::uint16_t>((i % 5) + 1);  // heterogeneidade

    // Encerramento limpo: encaminha SIGINT/SIGTERM aos filhos.
    std::signal(SIGINT, encerrar_filhos);
    std::signal(SIGTERM, encerrar_filhos);

    std::cout << "orquestrador: " << o.workers << " workers, "
              << (o.topologia == orquestrador::Topologia::Anel ? "anel" : "malha")
              << ", " << o.tarefas << " tarefas, telemetria UDP "
              << o.host << ":" << o.telemetria << "\n";

    for (int i = 0; i < o.workers; ++i) {
        orquestrador::EspecNo spec;
        spec.id             = static_cast<comum::NodeId>(o.porta_base + i);
        spec.host           = o.host;
        spec.porta_controle = static_cast<comum::Porta>(o.controle_base + i);
        spec.fator          = fatores[i];
        spec.tarefas        = tarefas[i];
        spec.monitor        = {o.host, o.telemetria};
        for (int j : adj[i])
            spec.vizinhos.push_back({o.host, static_cast<comum::Porta>(o.porta_base + j)});

        try {
            pid_t pid = orquestrador::spawnar_no(caminho_entidade, spec);
            g_pids[g_npids.fetch_add(1)] = pid;
            std::cout << "  no " << spec.id << " (controle " << spec.porta_controle
                      << ", fator " << spec.fator << ", tarefas " << spec.tarefas
                      << ", vizinhos " << spec.vizinhos.size() << ") pid " << pid << "\n";
        } catch (const std::exception& e) {
            std::cerr << "falha ao lancar no " << spec.id << ": " << e.what() << "\n";
            encerrar_filhos(0);
            return 1;
        }
    }

    escrever_cluster_json(o, fatores, tarefas);
    std::cout << "cluster.json escrito. Ctrl-C encerra todos os workers.\n";

    // Aguarda os filhos; reapa ate todos sairem (ex.: apos Ctrl-C -> SIGTERM).
    int restantes = o.workers;
    while (restantes > 0) {
        int   status;
        pid_t p = ::waitpid(-1, &status, 0);
        (void)status;
        if (p > 0) {
            --restantes;
        } else if (p < 0 && errno != EINTR) {
            break;
        }
    }
    std::cout << "orquestrador: todos os workers encerraram.\n";
    return 0;
}
