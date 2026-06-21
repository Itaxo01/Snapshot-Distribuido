// Implementacao do motor Chandy-Lamport.
#include "snapshot/chandy_lamport.hpp"

#include <utility>

namespace snapshot {

ChandyLamport::ChandyLamport(comum::NodeId eu,
                             std::vector<comum::NodeId> canais_entrada,
                             std::vector<comum::NodeId> canais_saida,
                             CapturarLocal  capturar_local,
                             EnviarMarcador enviar_marcador,
                             AoCompletar    ao_completar)
    : eu_(eu),
      entradas_(std::move(canais_entrada)),
      saidas_(std::move(canais_saida)),
      capturar_local_(std::move(capturar_local)),
      enviar_marcador_(std::move(enviar_marcador)),
      ao_completar_(std::move(ao_completar)) {}

void ChandyLamport::iniciar_gravacao(Execucao& ex, comum::SnapshotId id,
                                     std::optional<comum::NodeId> canal_gatilho) {
    // 1) Grava o estado local (corte limpo: estamos na thread consumidora).
    ex.estado_local  = capturar_local_();
    ex.local_gravado = true;

    // 2) Comeca a gravar TODOS os canais de entrada (lista todos, mesmo vazios).
    for (comum::NodeId e : entradas_) {
        ex.faltam.insert(e);
        ex.gravando.insert(e);
        ex.canais[e];  // cria entrada vazia (canal sem mensagens em transito)
    }

    // 3) O canal que disparou o inicio tem estado VAZIO e ja deu seu marcador.
    if (canal_gatilho) {
        ex.gravando.erase(*canal_gatilho);
        ex.faltam.erase(*canal_gatilho);
    }

    // 4) Regra de envio: marcador em todas as saidas, antes de qualquer outra
    //    mensagem de aplicacao nesses canais (garantido pela thread unica).
    for (comum::NodeId s : saidas_)
        enviar_marcador_(s, id);
}

void ChandyLamport::iniciar(comum::SnapshotId id) {
    if (concluidos_.count(id) || ativos_.count(id))
        return;  // ja iniciado ou concluido
    Execucao& ex = ativos_[id];
    iniciar_gravacao(ex, id, /*canal_gatilho=*/std::nullopt);
    verificar_conclusao(id, ex);
}

void ChandyLamport::ao_receber_marcador(comum::NodeId origem, comum::SnapshotId id) {
    if (concluidos_.count(id))
        return;  // marcador tardio apos conclusao: ignora

    auto it = ativos_.find(id);
    if (it == ativos_.end()) {
        // Primeiro marcador deste snapshot neste processo.
        Execucao& ex = ativos_[id];
        iniciar_gravacao(ex, id, /*canal_gatilho=*/origem);
        verificar_conclusao(id, ex);
    } else {
        // Ja havia gravado o estado local: finaliza o canal 'origem'.
        Execucao& ex = it->second;
        ex.gravando.erase(origem);
        ex.faltam.erase(origem);
        verificar_conclusao(id, ex);
    }
}

void ChandyLamport::ao_receber_negocio(comum::NodeId origem, const rede::Buffer& corpo) {
    // Registra a mensagem no estado de TODO snapshot que esteja gravando este
    // canal de entrada (pode haver varios concorrentes).
    for (auto& [id, ex] : ativos_) {
        (void)id;
        if (ex.gravando.count(origem))
            ex.canais[origem].push_back(corpo);
    }
}

void ChandyLamport::verificar_conclusao(comum::SnapshotId id, Execucao& ex) {
    if (!ex.local_gravado || !ex.faltam.empty())
        return;  // ainda incompleto

    // Move o conteudo para a peca ANTES de apagar a execucao do mapa.
    PecaLocal peca;
    peca.id           = id;
    peca.no           = eu_;
    peca.estado_local = std::move(ex.estado_local);
    peca.canais       = std::move(ex.canais);

    ativos_.erase(id);          // invalida 'ex' (nao usar depois)
    concluidos_.insert(id);
    ao_completar_(peca);
}

}  // namespace snapshot
