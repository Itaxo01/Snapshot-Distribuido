// Entry point da entidade (worker). O orquestrador lanca um processo destes por
// no, passando a configuracao via argv.
//
// Uso:
//   entidade --id <porta_negocio> [--host <bind=127.0.0.1>]
//            --controle <porta_controle> --fator <n> --tarefas <n>
//            --monitor <host:porta> [--vizinho <host:porta>]...
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "comum/config.hpp"
#include "entidade/worker.hpp"

namespace {

std::atomic<entidade::Worker*> g_worker{nullptr};

void ao_sinal(int) {
    if (auto* w = g_worker.load()) w->parar();
}

[[noreturn]] void uso(const char* prog) {
    std::cerr << "uso: " << prog
              << " --id <porta> [--host <bind>] --controle <porta>"
                 " --fator <n> --tarefas <n> --monitor <host:porta>"
                 " [--vizinho <host:porta>]...\n";
    std::exit(2);
}

comum::Endereco parse_endereco(const std::string& s) {
    auto pos = s.rfind(':');
    if (pos == std::string::npos) throw std::runtime_error("endereco invalido: " + s);
    return comum::Endereco{s.substr(0, pos),
                           static_cast<comum::Porta>(std::stoi(s.substr(pos + 1)))};
}

comum::ConfigWorker parse_args(int argc, char** argv) {
    comum::ConfigWorker cfg;
    cfg.escuta.host = "127.0.0.1";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto prox = [&]() -> std::string {
            if (i + 1 >= argc) uso(argv[0]);
            return argv[++i];
        };
        if (a == "--id")            cfg.escuta.porta     = static_cast<comum::Porta>(std::stoi(prox()));
        else if (a == "--host")     cfg.escuta.host      = prox();
        else if (a == "--controle") cfg.porta_controle   = static_cast<comum::Porta>(std::stoi(prox()));
        else if (a == "--fator")    cfg.fator            = static_cast<std::uint16_t>(std::stoi(prox()));
        else if (a == "--tarefas")  cfg.tarefas_iniciais = static_cast<comum::Contagem>(std::stoul(prox()));
        else if (a == "--monitor")  cfg.monitor_telemetria = parse_endereco(prox());
        else if (a == "--vizinho")  cfg.vizinhos.push_back(parse_endereco(prox()));
        else { std::cerr << "argumento desconhecido: " << a << "\n"; uso(argv[0]); }
    }

    if (cfg.escuta.porta == 0 || cfg.porta_controle == 0 ||
        cfg.monitor_telemetria.porta == 0)
        uso(argv[0]);
    return cfg;
}

}  // namespace

int main(int argc, char** argv) {
    comum::ConfigWorker cfg;
    try {
        cfg = parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "config invalida: " << e.what() << "\n";
        return 2;
    }

    try {
        entidade::Worker worker(cfg);
        g_worker.store(&worker);
        std::signal(SIGINT, ao_sinal);
        std::signal(SIGTERM, ao_sinal);

        worker.preparar();
        worker.executar();
    } catch (const std::exception& e) {
        std::cerr << "entidade " << cfg.escuta.porta << " falhou: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
