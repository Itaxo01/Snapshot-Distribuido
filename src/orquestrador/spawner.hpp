// fork/exec de processos worker (entidade) independentes no SO.
//
// Este e o unico ponto inerentemente local do sistema: lancar processos via
// fork/exec so funciona na mesma maquina. Num cenario distribuido real, daria
// lugar a SSH/lancamento manual -- o resto (protocolo, identidade, logica) nao
// muda. Ver requisitos.md, "Identidade vs. Localizacao".
#pragma once

#include <sys/types.h>  // pid_t

#include <cstdint>
#include <string>
#include <vector>

#include "comum/tipos.hpp"

namespace orquestrador {

// Especificacao completa de um no a ser lancado.
struct EspecNo {
    comum::NodeId                 id;              // == porta de negocio
    std::string                   host;           // bind/identidade na rede
    comum::Porta                  porta_controle;
    std::uint16_t                 fator;          // heterogeneidade
    comum::Contagem               tarefas;        // carga inicial
    std::vector<comum::Endereco>  vizinhos;       // endpoints de negocio dos vizinhos
    comum::Endereco               monitor;        // destino UDP da telemetria
};

// fork/exec do binario em 'caminho_entidade' com o argv derivado de 'spec'.
// Devolve o pid do filho. Lanca std::runtime_error em falha de fork.
pid_t spawnar_no(const std::string& caminho_entidade, const EspecNo& spec);

}  // namespace orquestrador
