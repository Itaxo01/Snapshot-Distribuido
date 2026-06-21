// Implementacao da politica de work-stealing (decisoes puras).
#include "entidade/work_stealing.hpp"

namespace entidade {

comum::Contagem lote_para_empurrar(comum::Contagem pendentes, const ParametrosRoubo& p) {
    if (pendentes <= p.limite_alto) return 0;
    comum::Contagem lote = static_cast<comum::Contagem>(pendentes * p.fracao_lote);
    if (lote < p.lote_min) lote = p.lote_min;
    if (lote > pendentes) lote = pendentes;
    return lote;
}

bool deve_pedir(comum::Contagem pendentes, const ParametrosRoubo& p) {
    return pendentes <= p.limite_baixo;
}

comum::Contagem lote_para_doar(comum::Contagem pendentes, comum::Contagem sugerido,
                               const ParametrosRoubo& p) {
    if (pendentes <= p.reserva_resposta) return 0;
    comum::Contagem disponivel = pendentes - p.reserva_resposta;
    comum::Contagem lote = sugerido > 0
                               ? sugerido
                               : static_cast<comum::Contagem>(pendentes * p.fracao_lote);
    if (lote < p.lote_min) lote = p.lote_min;
    if (lote > disponivel) lote = disponivel;
    return lote;
}

}  // namespace entidade
