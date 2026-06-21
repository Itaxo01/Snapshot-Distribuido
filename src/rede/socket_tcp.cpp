// Implementacao da camada de transporte TCP (Berkeley Sockets).
#include "rede/socket_tcp.hpp"

#include <arpa/inet.h>   // inet_ntop
#include <netdb.h>       // getaddrinfo, gai_strerror
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>      // close

#include <cerrno>
#include <chrono>
#include <cstring>       // strerror
#include <string>
#include <thread>        // this_thread::sleep_for

namespace rede {
namespace {

[[noreturn]] void lancar_errno(const std::string& msg) {
    throw ErroRede(msg + ": " + std::strerror(errno));
}

std::string descreve(const comum::Endereco& e) {
    return e.host + ":" + std::to_string(e.porta);
}

// Guard RAII para a lista devolvida por getaddrinfo.
struct InfoEndereco {
    addrinfo* lista = nullptr;
    ~InfoEndereco() { if (lista) ::freeaddrinfo(lista); }
};

// Resolve um Endereco para uma lista de addrinfo (IPv4, TCP). Lanca ErroRede.
addrinfo* resolver(const comum::Endereco& e, bool passivo, InfoEndereco& guard) {
    addrinfo hints{};
    hints.ai_family   = AF_INET;      // IPv4 (ver decisao de identidade = porta)
    hints.ai_socktype = SOCK_STREAM;  // TCP
    if (passivo) hints.ai_flags = AI_PASSIVE;

    std::string porta = std::to_string(e.porta);
    const char* host  = e.host.empty() ? nullptr : e.host.c_str();

    int rc = ::getaddrinfo(host, porta.c_str(), &hints, &guard.lista);
    if (rc != 0)
        throw ErroRede("getaddrinfo(" + descreve(e) + "): " + ::gai_strerror(rc));
    return guard.lista;
}

// Envia exatamente n bytes; trata writes parciais e EINTR; suprime SIGPIPE.
void enviar_tudo(int fd, const std::uint8_t* p, std::size_t n) {
    std::size_t enviados = 0;
    while (enviados < n) {
        ssize_t r = ::send(fd, p + enviados, n - enviados, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EINTR) continue;
            lancar_errno("send");
        }
        enviados += static_cast<std::size_t>(r);
    }
}

// Le ate n bytes. Devolve quantos leu: == n no caso normal, < n se o par
// fechou (EOF). Trata EINTR; lanca ErroRede em erro de I/O real.
std::size_t ler_tudo(int fd, std::uint8_t* p, std::size_t n) {
    std::size_t lidos = 0;
    while (lidos < n) {
        ssize_t r = ::recv(fd, p + lidos, n - lidos, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            lancar_errno("recv");
        }
        if (r == 0) break;  // EOF
        lidos += static_cast<std::size_t>(r);
    }
    return lidos;
}

}  // namespace

// --- Socket -----------------------------------------------------------------
Socket& Socket::operator=(Socket&& o) noexcept {
    if (this != &o) {
        fechar();
        fd_ = o.fd_;
        o.fd_ = -1;
    }
    return *this;
}

void Socket::fechar() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

int Socket::liberar() {
    int f = fd_;
    fd_ = -1;
    return f;
}

// --- Estabelecimento de conexao --------------------------------------------
Socket escutar_tcp(const comum::Endereco& bind, int backlog) {
    InfoEndereco guard;
    for (addrinfo* it = resolver(bind, /*passivo=*/true, guard); it; it = it->ai_next) {
        int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        int yes = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (::bind(fd, it->ai_addr, it->ai_addrlen) == 0 &&
            ::listen(fd, backlog) == 0) {
            return Socket(fd);
        }
        ::close(fd);
    }
    lancar_errno("escutar_tcp(" + descreve(bind) + ")");
}

Socket aceitar(const Socket& servidor, comum::Endereco* origem) {
    sockaddr_in sa{};
    socklen_t   len = sizeof(sa);
    int fd;
    do {
        fd = ::accept(servidor.fd(), reinterpret_cast<sockaddr*>(&sa), &len);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) lancar_errno("accept");

    if (origem) {
        char buf[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
        origem->host  = buf;
        origem->porta = ntohs(sa.sin_port);
    }
    return Socket(fd);
}

Socket conectar_tcp(const comum::Endereco& destino, int tentativas, int espera_ms) {
    for (int t = 0; t < tentativas; ++t) {
        InfoEndereco guard;
        for (addrinfo* it = resolver(destino, /*passivo=*/false, guard); it;
             it = it->ai_next) {
            int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (fd < 0) continue;
            if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0)
                return Socket(fd);
            ::close(fd);
        }
        if (t + 1 < tentativas && espera_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(espera_ms));
    }
    lancar_errno("conectar_tcp(" + descreve(destino) + ")");
}

// --- I/O de frames ----------------------------------------------------------
void enviar_frame(int fd, const Buffer& corpo) {
    Buffer q = quadro_tcp(corpo);
    enviar_tudo(fd, q.data(), q.size());
}

std::optional<Buffer> receber_frame(int fd) {
    std::uint8_t pfx[TAM_PREFIXO];
    std::size_t r = ler_tudo(fd, pfx, TAM_PREFIXO);
    if (r == 0) return std::nullopt;                      // EOF limpo
    if (r < TAM_PREFIXO) throw ErroRede("frame truncado (prefixo)");

    std::uint32_t tam = ler_prefixo(pfx);
    if (tam > MAX_QUADRO)
        throw ErroRede("frame grande demais: " + std::to_string(tam) + " bytes");

    Buffer corpo(tam);
    if (tam > 0) {
        std::size_t r2 = ler_tudo(fd, corpo.data(), tam);
        if (r2 < tam) throw ErroRede("frame truncado (corpo)");
    }
    return corpo;
}

}  // namespace rede
