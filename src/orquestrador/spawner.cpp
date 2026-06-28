// Implementacao do spawner (fork/exec).
#include "orquestrador/spawner.hpp"

#include <unistd.h>

#include <cstdlib>
#include <stdexcept>

namespace orquestrador {
namespace {

std::string endereco_str(const comum::Endereco& e) {
    return e.host + ":" + std::to_string(e.porta);
}

}  // namespace

pid_t spawnar_no(const std::string& caminho_entidade, const EspecNo& spec) {
    // Monta o argv como strings (donas) e so entao os ponteiros para execv.
    std::vector<std::string> args = {
        caminho_entidade,
        "--id",       std::to_string(spec.id),
        "--host",     spec.host,
        "--controle", std::to_string(spec.porta_controle),
        "--fator",    std::to_string(spec.fator),
        "--monitor",  endereco_str(spec.monitor),
    };
    for (const auto& v : spec.vizinhos) {
        args.push_back("--vizinho");
        args.push_back(endereco_str(v));
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);

    pid_t pid = ::fork();
    if (pid < 0)
        throw std::runtime_error("fork falhou");
    if (pid == 0) {
        // Filho: vira o processo entidade.
        ::execv(caminho_entidade.c_str(), argv.data());
        // So chega aqui se execv falhar.
        std::_Exit(127);
    }
    return pid;  // pai
}

}  // namespace orquestrador
