// Constroi o grafo de vizinhos entre os workers.
//
// Pre-requisito do snapshot: o grafo (visto como bidirecional, ja que cada par
// usa UMA conexao com dois canais direcionados) deve ser CONEXO -- assim e
// fortemente conexo e o snapshot pode iniciar a partir de qualquer no.
#pragma once

#include <vector>

namespace orquestrador {

enum class Topologia {
    Anel,   // cada no liga aos dois adjacentes (grau 2); minimo conexo
    Malha,  // cada no liga a todos os outros (grau n-1); mais trafego em transito
};

// Lista de adjacencia por INDICE de no [0, n). Simetrica (grafo nao-direcionado).
std::vector<std::vector<int>> construir_topologia(int n, Topologia t);

}  // namespace orquestrador
