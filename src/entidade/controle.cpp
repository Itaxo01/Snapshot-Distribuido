// Implementacao da conexao de controle Monitor <-> worker.
#include "entidade/controle.hpp"

#include <poll.h>
#include <sys/socket.h>  // shutdown

#include <utility>

#include "rede/protocolo.hpp"

namespace entidade {

Controle::Controle(const comum::Endereco& escuta_controle)
    : servidor_(rede::escutar_tcp(escuta_controle)) {}

Controle::~Controle() { parar(); }

void Controle::iniciar() {
    th_ = std::thread([this] { laco(); });
}

void Controle::laco() {
    // Fase 1: aceitar a conexao do Monitor. poll() com timeout para conseguir
    // checar 'parado_' periodicamente sem fechar o socket de escuta sob accept.
    rede::Socket conn;
    while (!parado_.load(std::memory_order_relaxed)) {
        pollfd pfd{servidor_.fd(), POLLIN, 0};
        int r = ::poll(&pfd, 1, 200);
        if (r > 0 && (pfd.revents & POLLIN)) {
            try {
                conn = rede::aceitar(servidor_);
                break;
            } catch (const rede::ErroRede&) {
                return;
            }
        }
        // r == 0: timeout -> recheca parado_;  r < 0: EINTR -> continua
    }
    if (parado_.load(std::memory_order_relaxed) || !conn.valido()) return;

    fd_conexao_.store(conn.fd());
    conexao_ = std::move(conn);  // mantem o fd vivo (RAII)

    // Fase 2: ler comandos do Monitor ate fechar/parar.
    try {
        while (!parado_.load(std::memory_order_relaxed)) {
            std::optional<rede::Buffer> corpo = rede::receber_frame(fd_conexao_.load());
            if (!corpo) break;  // Monitor fechou
            if (rede::tipo_de(*corpo) == rede::TipoMsg::INICIAR_SNAPSHOT) {
                if (auto m = rede::parse_iniciar_snapshot(*corpo))
                    comandos_.push(Comando{Comando::Tipo::INICIAR_SNAPSHOT, m->snapshot_id});
            }
        }
    } catch (const rede::ErroRede&) {
        // conexao quebrou ou shutdown em parar(): encerra o laco.
    }
    fd_conexao_.store(-1);
}

void Controle::parar() {
    if (parado_.exchange(true)) return;  // idempotente
    if (servidor_.valido())
        ::shutdown(servidor_.fd(), SHUT_RDWR);
    int fd = fd_conexao_.load();
    if (fd >= 0)
        ::shutdown(fd, SHUT_RDWR);  // desbloqueia o receber_frame
    if (th_.joinable())
        th_.join();
}

}  // namespace entidade
