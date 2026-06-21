// Canal direcionado sobre uma conexao TCP com um vizinho.
//
// Um Canal representa a conexao (par) com UM vizinho e roda uma thread leitora
// dedicada. A leitora aplica um ATRASO aleatorio uniforme (simulacao de
// latencia de rede) a cada frame recebido e o empurra na caixa de entrada unica
// e compartilhada do worker. O ENVIO e imediato (latencia so no recebimento).
//
// Ordem por-canal (FIFO) e garantida porque a leitora e sequencial: le o frame
// N, espera, empurra N, so entao le N+1. Marcadores fluem por esse mesmo
// caminho, preservando sua ordem relativa aos dados -- requisito do snapshot.
//
// Posse: Canal e alocado no heap pelas fabricas (a thread captura um 'this'
// estavel); nao e copiavel nem movivel.
#pragma once

#include <atomic>
#include <memory>
#include <random>
#include <thread>

#include "comum/config.hpp"          // comum::Vizinho
#include "comum/parametros.hpp"      // comum::parametro
#include "comum/tipos.hpp"           // comum::NodeId
#include "rede/fila_bloqueante.hpp"
#include "rede/protocolo.hpp"        // Buffer
#include "rede/socket_tcp.hpp"       // Socket

namespace rede {

// Atraso padrao de simulacao de latencia (uniforme, em ms). Ver comum::parametro.
constexpr int ATRASO_MIN_MS = comum::parametro::REDE_ATRASO_MIN_MS;
constexpr int ATRASO_MAX_MS = comum::parametro::REDE_ATRASO_MAX_MS;

// Um frame de negocio recebido, com o vizinho de origem. O 'corpo' e o frame
// cru ([u8 tipo][campos]); quem consome usa rede::tipo_de / parse_*.
struct MensagemRecebida {
    comum::NodeId origem;
    Buffer        corpo;
};

// Caixa de entrada unica do worker (todas as leitoras de canal empurram aqui).
using FilaInbox = FilaBloqueante<MensagemRecebida>;

class Canal {
public:
    Canal(const Canal&) = delete;
    Canal& operator=(const Canal&) = delete;
    ~Canal();

    // Lado que CONECTA (menor id -> maior id): disca, envia OLA{meu_id} e inicia
    // a leitora. Conhece o vizinho de antemao (== viz.porta).
    static std::unique_ptr<Canal> conectar(comum::NodeId meu_id,
                                           const comum::Vizinho& viz,
                                           FilaInbox& inbox,
                                           int atraso_min_ms = ATRASO_MIN_MS,
                                           int atraso_max_ms = ATRASO_MAX_MS);

    // Lado que ACEITA: recebe um socket ja aceito, le o OLA do handshake para
    // descobrir a identidade do vizinho e inicia a leitora.
    static std::unique_ptr<Canal> aceitar(Socket sock, FilaInbox& inbox,
                                          int atraso_min_ms = ATRASO_MIN_MS,
                                          int atraso_max_ms = ATRASO_MAX_MS);

    comum::NodeId vizinho() const { return vizinho_; }

    // Envia um frame de negocio a este vizinho (imediato, sem atraso).
    // Chamado APENAS pela thread consumidora. Lanca ErroRede em falha de I/O.
    void enviar(const Buffer& corpo);

    // Encerra a leitora (shutdown + join) e fecha o socket. Idempotente.
    void parar();

private:
    Canal(Socket sock, comum::NodeId vizinho, FilaInbox& inbox,
          int atraso_min_ms, int atraso_max_ms);

    void iniciar();        // inicia a thread leitora (apos endereco estavel)
    void laco_leitor();    // corpo da thread leitora
    void aplicar_atraso(); // dorme um intervalo uniforme [min, max] ms

    Socket            sock_;
    comum::NodeId     vizinho_;
    FilaInbox&        inbox_;
    int               atraso_min_ms_;
    int               atraso_max_ms_;
    std::mt19937      rng_;
    std::thread       leitor_;
    std::atomic<bool> parado_{false};
};

}  // namespace rede
