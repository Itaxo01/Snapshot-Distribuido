// Telemetria UDP: caminho fire-and-forget worker -> Monitor.
//
// UDP (e nao TCP) e proposital: o monitoramento nao pode bloquear nem derrubar
// a rede TCP de negocio. Cada mensagem e UM datagrama (auto-delimitado, sem
// enquadramento). Perdas sao aceitaveis: o campo 'seq' deixa o receptor
// descartar pacotes atrasados/duplicados e a proxima amostra supera a perdida.
#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "comum/tipos.hpp"
#include "rede/protocolo.hpp"   // Buffer, Msg* de telemetria
#include "rede/socket_tcp.hpp"  // Socket (RAII), ErroRede

namespace rede {

// Lado worker. Resolve o destino e conecta o socket UDP UMA vez; depois cada
// envio e um simples send(). Nunca lanca apos a construcao (fire-and-forget).
class TelemetriaEmissor {
public:
    explicit TelemetriaEmissor(const comum::Endereco& destino);  // lanca no setup

    void enviar(const Buffer& corpo) noexcept;
    void enviar(const MsgTeleEstado& m) noexcept;  // serializa e envia
    void enviar(const MsgTeleEvento& m) noexcept;

private:
    Socket sock_;
};

// Lado Monitor. Faz bind e entrega o corpo cru de cada datagrama; quem chama
// usa rede::tipo_de / parse_tele_*.
class TelemetriaReceptor {
public:
    explicit TelemetriaReceptor(comum::Porta porta,
                                const std::string& host = "0.0.0.0");

    // Timeout de recepcao (SO_RCVTIMEO) para o laco poder checar uma flag de
    // parada. Zero = bloqueante.
    void definir_timeout(std::chrono::milliseconds t);

    // Bloqueia ate um datagrama (ou timeout). Devolve o corpo, ou std::nullopt
    // em timeout/erro. Preenche 'origem' se != nullptr.
    std::optional<Buffer> receber(comum::Endereco* origem = nullptr);

    int fd() const { return sock_.fd(); }

private:
    Socket sock_;
};

}  // namespace rede
