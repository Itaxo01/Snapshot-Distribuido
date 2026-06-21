// Politica de balanceamento de carga (work-stealing): push + pull.
//
// Funcoes PURAS de decisao -- sem I/O, sem estado. O worker as consulta e
// executa o envio. Manter a politica separada deixa-a facil de explicar no
// seminario e de ajustar sem mexer na mecanica de rede.
#pragma once

#include "comum/parametros.hpp"
#include "comum/tipos.hpp"

namespace entidade {

// Defaults vindos de comum::parametro; sobrescrevivel por instancia.
struct ParametrosRoubo {
    comum::Contagem limite_alto      = comum::parametro::WS_LIMITE_ALTO;
    comum::Contagem limite_baixo     = comum::parametro::WS_LIMITE_BAIXO;
    double          fracao_lote      = comum::parametro::WS_FRACAO_LOTE;
    comum::Contagem lote_min         = comum::parametro::WS_LOTE_MIN;
    comum::Contagem reserva_resposta = comum::parametro::WS_RESERVA_RESPOSTA;
};

// PUSH: tamanho do lote a empurrar proativamente, ou 0 se nao deve empurrar.
comum::Contagem lote_para_empurrar(comum::Contagem pendentes, const ParametrosRoubo&);

// PULL: true se o no esta ocioso e deve pedir trabalho a um vizinho.
bool deve_pedir(comum::Contagem pendentes, const ParametrosRoubo&);

// Resposta a um PEDIDO_ROUBO: tamanho do lote a doar, ou 0 se nao puder.
comum::Contagem lote_para_doar(comum::Contagem pendentes,
                               comum::Contagem sugerido, const ParametrosRoubo&);

}  // namespace entidade
