// Camada de transporte TCP sobre Berkeley Sockets.
//
// Responsabilidades:
//   - Posse RAII de file descriptors (Socket). (Resource Acquisition is Initialization)
//   - Estabelecimento de conexao: escutar / aceitar / conectar.
//   - I/O de FRAMES: aplica o enquadramento [u32 tamanho][corpo] do protocolo,
//     lidando com reads/writes parciais, EINTR e SIGPIPE.
//
// Concorrencia: e seguro uma thread chamar receber_frame() e OUTRA chamar
// enviar_frame() no mesmo fd (um leitor + um escritor por conexao), que e
// exatamente o modelo do projeto (thread leitora por canal + thread consumidora
// unica que escreve).
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>

#include "comum/tipos.hpp"     // comum::Endereco
#include "rede/protocolo.hpp"  // rede::Buffer e o enquadramento

namespace rede {

// Limite de seguranca para o corpo de um frame TCP: um prefixo de tamanho
// corrompido nao pode fazer o receptor tentar alocar gigabytes.
constexpr std::uint32_t MAX_QUADRO = 1u << 20;  // 1 MiB

// Erros de rede (setup ou I/O). Mensagem inclui o errno quando aplicavel.
struct ErroRede : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Posse RAII de um fd de socket. Movel, nao copiavel; fecha no destrutor.
class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { fechar(); }

    Socket(Socket&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    Socket& operator=(Socket&& o) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int  fd() const { return fd_; }
    bool valido() const { return fd_ >= 0; }
    int  liberar();   // cede a posse do fd (nao fecha) e devolve-o
    void fechar();

private:
    int fd_ = -1;
};

// --- Estabelecimento de conexao --------------------------------------------

// Socket TCP escutando em 'bind' (SO_REUSEADDR + listen). Lanca ErroRede.
//   bind.host: "127.0.0.1" (so local) ou "0.0.0.0" (aceitar de outras maquinas).
Socket escutar_tcp(const comum::Endereco& bind, int backlog = 16);

// Aceita uma conexao em 'servidor' (BLOQUEANTE). Se 'origem' != nullptr,
// preenche-o com o endereco do par. Lanca ErroRede em falha.
Socket aceitar(const Socket& servidor, comum::Endereco* origem = nullptr);

// Conecta a 'destino'. Tenta ate 'tentativas' vezes com 'espera_ms' entre elas
// -- util na inicializacao, quando o par pode ainda nao estar escutando (a
// regra "menor id conecta no maior" tem corrida de partida). Lanca ErroRede se
// todas as tentativas falharem.
Socket conectar_tcp(const comum::Endereco& destino, int tentativas = 3,
                    int espera_ms = 200);

// --- I/O de frames ----------------------------------------------------------

// Envia 'corpo' como um frame TCP completo ([u32 tamanho][corpo]). Trata writes
// parciais, EINTR e suprime SIGPIPE. Lanca ErroRede em falha de escrita.
void enviar_frame(int fd, const Buffer& corpo);

// Recebe um frame TCP completo e devolve o corpo (sem o prefixo).
//   - std::nullopt: o par fechou a conexao ordenadamente (EOF no limite de um
//     frame). Sinaliza fim do canal, nao erro.
//   - Lanca ErroRede em: frame truncado, tamanho > MAX_QUADRO, ou erro de I/O.
std::optional<Buffer> receber_frame(int fd);

}  // namespace rede
