// Implementacao da construcao de topologia.
#include "orquestrador/topologia.hpp"

#include <set>

namespace orquestrador {

std::vector<std::vector<int>> construir_topologia(int n, Topologia t) {
    std::vector<std::set<int>> adj(n);

    if (t == Topologia::Anel) {
        // Liga cada no aos dois adjacentes no ciclo. O set deduplica o caso n==2
        // (em que (i+1) e (i-1) coincidem).
        for (int i = 0; i < n; ++i) {
            if (n < 2) break;
            int prox = (i + 1) % n;
            int ant  = (i - 1 + n) % n;
            if (prox != i) { adj[i].insert(prox); adj[prox].insert(i); }
            if (ant  != i) { adj[i].insert(ant);  adj[ant].insert(i);  }
        }
    } else {  // Malha
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (i != j) adj[i].insert(j);
    }

    std::vector<std::vector<int>> r(n);
    for (int i = 0; i < n; ++i) r[i].assign(adj[i].begin(), adj[i].end());
    return r;
}

}  // namespace orquestrador
