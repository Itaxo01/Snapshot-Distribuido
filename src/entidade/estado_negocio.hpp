// Estado de negocio local do worker: as duas variaveis rastreadas.
//
// codificar()/decodificar() convertem para/de o blob OPACO que o motor de
// Chandy-Lamport guarda como "estado local" (8 bytes big-endian). O motor nunca
// interpreta esse blob; quem o entende e o worker (na captura e na conclusao).
#pragma once

#include <cstdint>

#include "comum/tipos.hpp"
#include "rede/protocolo.hpp"  // rede::Buffer

namespace entidade {

struct EstadoNegocio {
    comum::Contagem pendentes  = 0;
    comum::Contagem concluidas = 0;

    rede::Buffer codificar() const {
        rede::Buffer b;
        auto put32 = [&](comum::Contagem v) {
            b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
            b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
            b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
            b.push_back(static_cast<std::uint8_t>(v & 0xFF));
        };
        put32(pendentes);
        put32(concluidas);
        return b;
    }

    static EstadoNegocio decodificar(const rede::Buffer& b) {
        EstadoNegocio e;
        if (b.size() >= 8) {
            auto get32 = [&](std::size_t i) {
                return (comum::Contagem(b[i]) << 24) | (comum::Contagem(b[i + 1]) << 16) |
                       (comum::Contagem(b[i + 2]) << 8) | comum::Contagem(b[i + 3]);
            };
            e.pendentes  = get32(0);
            e.concluidas = get32(4);
        }
        return e;
    }
};

}  // namespace entidade
