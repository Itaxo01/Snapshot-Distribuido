// Fila FIFO thread-safe e bloqueante.
//
// Usada como a UNICA caixa de entrada do worker: todas as threads leitoras de
// canal empurram (push) mensagens recebidas, e a unica thread consumidora as
// retira (pop). Por ser FIFO e por cada leitora empurrar sua propria conexao em
// ordem, a ordem por-canal (FIFO) e preservada -- pre-requisito do snapshot.
#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace rede {

template <class T>
class FilaBloqueante {
public:
    void push(T v) {
        {
            std::lock_guard<std::mutex> lk(m_);
            fila_.push(std::move(v));
        }
        cv_.notify_one();
    }

    // Bloqueia ate haver item ou a fila ser fechada.
    // std::nullopt apenas quando fechada e vazia.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return !fila_.empty() || fechada_; });
        if (fila_.empty()) return std::nullopt;
        T v = std::move(fila_.front());
        fila_.pop();
        return v;
    }

    // Bloqueia ate haver item, expirar 'espera', ou a fila ser fechada.
    // std::nullopt em timeout ou fechada-e-vazia. Permite ao consumidor
    // intercalar trabalho periodico (processar tarefas) entre mensagens.
    template <class Rep, class Period>
    std::optional<T> pop_por(const std::chrono::duration<Rep, Period>& espera) {
        std::unique_lock<std::mutex> lk(m_);
        if (!cv_.wait_for(lk, espera, [&] { return !fila_.empty() || fechada_; }))
            return std::nullopt;  // timeout
        if (fila_.empty()) return std::nullopt;  // fechada
        T v = std::move(fila_.front());
        fila_.pop();
        return v;
    }

    // Acorda todos os consumidores; pops futuros devolvem nullopt quando vazia.
    void fechar() {
        {
            std::lock_guard<std::mutex> lk(m_);
            fechada_ = true;
        }
        cv_.notify_all();
    }

    std::size_t tamanho() const {
        std::lock_guard<std::mutex> lk(m_);
        return fila_.size();
    }

private:
    mutable std::mutex      m_;
    std::condition_variable cv_;
    std::queue<T>           fila_;
    bool                    fechada_ = false;
};

}  // namespace rede
