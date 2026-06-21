// Implementacao do canal direcionado sobre TCP.
#include "rede/canal.hpp"

#include <sys/socket.h>  // shutdown

#include <chrono>
#include <utility>

namespace rede {

Canal::Canal(Socket sock, comum::NodeId vizinho, FilaInbox& inbox,
             int atraso_min_ms, int atraso_max_ms)
    : sock_(std::move(sock)),
      vizinho_(vizinho),
      inbox_(inbox),
      atraso_min_ms_(atraso_min_ms),
      atraso_max_ms_(atraso_max_ms),
      rng_(std::random_device{}()) {}

Canal::~Canal() { parar(); }

std::unique_ptr<Canal> Canal::conectar(comum::NodeId meu_id,
                                       const comum::Vizinho& viz,
                                       FilaInbox& inbox, int atraso_min_ms,
                                       int atraso_max_ms) {
    // Tentativas generosas: na partida o vizinho pode ainda nao estar escutando.
    Socket s = conectar_tcp(viz, comum::parametro::CONEXAO_TENTATIVAS,
                            comum::parametro::CONEXAO_ESPERA_MS);
    enviar_frame(s.fd(), serializar(MsgOla{meu_id}));

    const comum::NodeId vizinho_id = viz.porta;  // identidade == porta
    std::unique_ptr<Canal> c(
        new Canal(std::move(s), vizinho_id, inbox, atraso_min_ms, atraso_max_ms));
    c->iniciar();
    return c;
}

std::unique_ptr<Canal> Canal::aceitar(Socket sock, FilaInbox& inbox,
                                      int atraso_min_ms, int atraso_max_ms) {
    // O primeiro frame DEVE ser o OLA do handshake; e consumido aqui (nunca vai
    // para a caixa de entrada / snapshot).
    std::optional<Buffer> corpo = receber_frame(sock.fd());
    if (!corpo)
        throw ErroRede("canal: par fechou antes do OLA");
    if (tipo_de(*corpo) != TipoMsg::OLA)
        throw ErroRede("canal: esperado OLA no handshake");
    std::optional<MsgOla> ola = parse_ola(*corpo);
    if (!ola)
        throw ErroRede("canal: OLA malformado");

    std::unique_ptr<Canal> c(new Canal(std::move(sock), ola->remetente, inbox,
                                       atraso_min_ms, atraso_max_ms));
    c->iniciar();
    return c;
}

void Canal::iniciar() {
    leitor_ = std::thread([this] { laco_leitor(); });
}

void Canal::laco_leitor() {
    try {
        while (!parado_.load(std::memory_order_relaxed)) {
            std::optional<Buffer> corpo = receber_frame(sock_.fd());
            if (!corpo) break;  // EOF: vizinho fechou a conexao

            aplicar_atraso();  // latencia de rede simulada (so no recebimento)
            if (parado_.load(std::memory_order_relaxed)) break;

            inbox_.push(MensagemRecebida{vizinho_, std::move(*corpo)});
        }
    } catch (const ErroRede&) {
        // Conexao quebrou ou foi encerrada (shutdown em parar()): sai do laco.
        // O projeto nao trata falhas de processo, entao apenas terminamos.
    }
}

void Canal::aplicar_atraso() {
    if (atraso_max_ms_ <= 0) return; // Aqui poderiamos fazer mais verificações, mas como eu contorlo o atraso vou deixar assim.
    std::uniform_int_distribution<int> d(atraso_min_ms_, atraso_max_ms_);
    std::this_thread::sleep_for(std::chrono::milliseconds(d(rng_)));
}

void Canal::enviar(const Buffer& corpo) {
    enviar_frame(sock_.fd(), corpo);
}

void Canal::parar() {
    if (parado_.exchange(true)) return;  // ja parado (idempotente)
    if (sock_.valido())
        ::shutdown(sock_.fd(), SHUT_RDWR);  // desbloqueia o recv da leitora
    if (leitor_.joinable())
        leitor_.join();
    sock_.fechar();
}

}  // namespace rede
