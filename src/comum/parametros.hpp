// Parametros de simulacao e ajuste, centralizados num so lugar.
//
// Nao e o ideal arquitetural (cada modulo poderia ter os seus), mas para um
// projeto pequeno concentrar os "botoes" aqui facilita ajustar o comportamento
// e o ritmo da demonstracao sem cacar constantes pelo codigo. Os nomes sao
// descritivos justamente para compensar a falta de encapsulamento.
//
// Uso: comum::parametro::NOME.
// inline constexpr (C++17): seguro incluir em qualquer TU sem violar a ODR.
#pragma once

#include <cstdint>

namespace comum::parametro {

// === Simulacao de latencia de rede ========================================
// Atraso aplicado a CADA mensagem recebida (uniforme em [min, max], ms). Maior
// = mensagens demoram mais em transito e o "limbo" do snapshot fica mais visivel.
inline constexpr int REDE_ATRASO_MIN_MS = 800;
inline constexpr int REDE_ATRASO_MAX_MS = 1200;

// === Processamento de tarefas =============================================
// Tarefas processadas por milissegundo, por unidade de 'fator' (heterogeneidade).
// Menor = a execucao dura mais (bom para observar a demo).
inline constexpr double TAREFAS_POR_MS_POR_FATOR = 0.1;

// === Ritmo do laco consumidor do worker (ms) ==============================
inline constexpr int WORKER_TICK_MS            = 10;    // espera max. por msg na inbox
inline constexpr int WORKER_INTERVALO_DIST_MS  = 1000;  // periodo do work-stealing
inline constexpr int WORKER_INTERVALO_TELE_MS  = 150;   // periodo da telemetria

// === Politica de work-stealing ============================================
inline constexpr std::uint32_t WS_LIMITE_ALTO      = 5000;  // acima: empurra (push)
inline constexpr std::uint32_t WS_LIMITE_BAIXO     = 0;     // <=: ocioso, pede (pull)
inline constexpr double        WS_FRACAO_LOTE      = 0.25;  // fracao das pendentes/lote
inline constexpr std::uint32_t WS_LOTE_MIN         = 100;   // tamanho minimo do lote
inline constexpr std::uint32_t WS_RESERVA_RESPOSTA = 500;   // so doa se sobrar isto

// === Estabelecimento de conexao (janela de partida) =======================
// Na inicializacao um par pode ainda nao estar escutando; tenta-se varias vezes.
// tentativas * espera define a janela maxima de espera por um par (ms).
inline constexpr int CONEXAO_TENTATIVAS = 100;
inline constexpr int CONEXAO_ESPERA_MS  = 100;

// === Monitor / UI =========================================================
inline constexpr float UI_ESCALA            = 1.4f;  // escala de fonte e widgets
inline constexpr bool  UI_MAXIMIZADA        = true;  // abre maximizada (mantem barra de titulo)
inline constexpr int   UI_JANELA_LARGURA    = 1100;  // tamanho "restaurado" se nao maximizada
inline constexpr int   UI_JANELA_ALTURA     = 680;
inline constexpr int   MONITOR_VARREDURA_MS = 300;   // periodo da varredura de snapshots/
inline constexpr int   MONITOR_MAX_LOG      = 1000;  // linhas mantidas no log de eventos
inline constexpr int   MONITOR_TELE_TIMEOUT_MS = 200;  // p/ a thread checar parada

}  // namespace comum::parametro
