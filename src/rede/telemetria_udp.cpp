// Implementacao da telemetria UDP.
#include "rede/telemetria_udp.hpp"

#include <arpa/inet.h>   // inet_ntop
#include <netdb.h>       // getaddrinfo
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>      // close

#include <cerrno>
#include <cstring>       // strerror

namespace rede {
namespace {

[[noreturn]] void lancar_errno(const std::string& msg) {
    throw ErroRede(msg + ": " + std::strerror(errno));
}

struct InfoEndereco {
    addrinfo* lista = nullptr;
    ~InfoEndereco() { if (lista) ::freeaddrinfo(lista); }
};

addrinfo* resolver_udp(const std::string& host, comum::Porta porta, bool passivo,
                       InfoEndereco& guard) {
    addrinfo hints{};
    hints.ai_family   = AF_INET;     // IPv4
    hints.ai_socktype = SOCK_DGRAM;  // UDP
    if (passivo) hints.ai_flags = AI_PASSIVE;

    std::string p = std::to_string(porta);
    const char* h = host.empty() ? nullptr : host.c_str();
    int rc = ::getaddrinfo(h, p.c_str(), &hints, &guard.lista);
    if (rc != 0)
        throw ErroRede("getaddrinfo(" + host + ":" + p + "): " + ::gai_strerror(rc));
    return guard.lista;
}

}  // namespace

// --- Emissor ----------------------------------------------------------------
TelemetriaEmissor::TelemetriaEmissor(const comum::Endereco& destino) {
    InfoEndereco guard;
    for (addrinfo* it = resolver_udp(destino.host, destino.porta, /*passivo=*/false,
                                     guard);
         it; it = it->ai_next) {
        int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        // connect() em UDP so fixa o peer default: nao envia nada, mas permite
        // usar send() depois e resolve o destino uma unica vez.
        if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            sock_ = Socket(fd);
            return;
        }
        ::close(fd);
    }
    lancar_errno("TelemetriaEmissor(" + destino.host + ":" +
                 std::to_string(destino.porta) + ")");
}

void TelemetriaEmissor::enviar(const Buffer& corpo) noexcept {
    if (!sock_.valido() || corpo.empty()) return;
    // Fire-and-forget: ignora o resultado. Um peer ausente pode gerar
    // ECONNREFUSED assincrono num send seguinte; nao nos importa.
    (void)::send(sock_.fd(), corpo.data(), corpo.size(), MSG_NOSIGNAL);
}

void TelemetriaEmissor::enviar(const MsgTeleEstado& m) noexcept {
    enviar(serializar(m));
}

void TelemetriaEmissor::enviar(const MsgTeleEvento& m) noexcept {
    enviar(serializar(m));
}

// --- Receptor ---------------------------------------------------------------
TelemetriaReceptor::TelemetriaReceptor(comum::Porta porta, const std::string& host) {
    InfoEndereco guard;
    for (addrinfo* it = resolver_udp(host, porta, /*passivo=*/true, guard); it;
         it = it->ai_next) {
        int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        int yes = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (::bind(fd, it->ai_addr, it->ai_addrlen) == 0) {
            sock_ = Socket(fd);
            return;
        }
        ::close(fd);
    }
    lancar_errno("TelemetriaReceptor(" + host + ":" + std::to_string(porta) + ")");
}

void TelemetriaReceptor::definir_timeout(std::chrono::milliseconds t) {
    timeval tv{};
    tv.tv_sec  = static_cast<time_t>(t.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((t.count() % 1000) * 1000);
    ::setsockopt(sock_.fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

std::optional<Buffer> TelemetriaReceptor::receber(comum::Endereco* origem) {
    std::uint8_t buf[2048];
    sockaddr_in  sa{};
    socklen_t    len = sizeof(sa);

    ssize_t r;
    do {
        r = ::recvfrom(sock_.fd(), buf, sizeof(buf), 0,
                       reinterpret_cast<sockaddr*>(&sa), &len);
    } while (r < 0 && errno == EINTR);

    if (r < 0) return std::nullopt;  // timeout (EAGAIN/EWOULDBLOCK) ou erro

    if (origem) {
        char host[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &sa.sin_addr, host, sizeof(host));
        origem->host  = host;
        origem->porta = ntohs(sa.sin_port);
    }
    return Buffer(buf, buf + r);
}

}  // namespace rede
