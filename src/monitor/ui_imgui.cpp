// Implementacao da UI ImGui (duas colunas).
#include "monitor/ui_imgui.hpp"

#include <cstdio>
#include <vector>

#include "imgui.h"

namespace monitor {
namespace {

long long soma_local(const std::map<comum::NodeId, EstadoWorker>& ws) {
    long long s = 0;
    for (const auto& [id, w] : ws) {
        (void)id;
        s += static_cast<long long>(w.pendentes) + w.concluidas;
    }
    return s;
}

// Lista rolavel de snapshots agregados. Cada item expande para as pecas.
void desenhar_lista_snapshots(const char* titulo, const char* id_child,
                              const std::vector<ResultadoSnapshot>& lista, float largura) {
    ImGui::BeginChild(id_child, ImVec2(largura, 380), true);
    ImGui::TextUnformatted(titulo);
    ImGui::Separator();
    if (lista.empty()) {
        ImGui::TextDisabled("(nenhum)");
    }
    for (const auto& r : lista) {
        ImGui::PushID(static_cast<int>(r.id));
        bool aberto = ImGui::TreeNode("no", "iniciado por %u", r.iniciador);
        ImGui::SameLine();
        if (r.terminado)
            ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "TERMINADO");
        else if (r.consistente)
            ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "processando (%lld pend, %lld transito)",
                               r.pendentes_totais, r.transito_totais);
        else
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "INCONSISTENTE %lld != %lld",
                               r.soma, r.total);
        if (aberto) {
            ImGui::TextDisabled("soma %lld == total %lld  (conservacao)", r.soma, r.total);
            for (const auto& p : r.pecas)
                ImGui::BulletText("%u: local %u   transito %u", p.no,
                                  p.pendentes + p.concluidas, p.em_transito);
            if (!r.caminho.empty()) ImGui::TextDisabled("%s", r.caminho.c_str());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void desenhar_workers(const std::map<comum::NodeId, EstadoWorker>& ws) {
    // Largura da coluna de rotulo derivada da fonte (cabe o pior caso, com [SNAP]),
    // para a barra nunca sobrepor o texto independentemente da escala da UI.
    float col_rotulo = ImGui::CalcTextSize("00000  x00  [SNAP]  ").x;

    for (const auto& [id, w] : ws) {
        comum::Contagem tot  = w.pendentes + w.concluidas;
        float           frac = tot ? static_cast<float>(w.concluidas) / tot : 0.0f;

        char rotulo[64];
        std::snprintf(rotulo, sizeof(rotulo), "%u  x%u%s", id, w.fator,
                      w.em_snapshot ? "  [SNAP]" : "");
        ImGui::Text("%s", rotulo);
        ImGui::SameLine(col_rotulo);

        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "pend %u / concl %u", w.pendentes, w.concluidas);
        ImGui::ProgressBar(frac, ImVec2(-1, 0), overlay);
    }
}

}  // namespace

void desenhar(ContextoUI& ctx) {
    auto      ws        = ctx.estado.copia_workers();
    auto      log       = ctx.estado.copia_log();
    long long soma_viva = soma_local(ws);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Monitor", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::Text("Snapshot Distribuido (Chandy-Lamport)   |   %s   |   total %u tarefas (injetadas)",
                ctx.info.c_str(), ctx.coletor.total());
    ImGui::Separator();

    float larg_esq = ImGui::GetContentRegionAvail().x * 0.58f;

    // ===================== COLUNA ESQUERDA =====================
    ImGui::BeginChild("esq", ImVec2(larg_esq, 0), true);
    {
        ImGui::SeparatorText("Workers");
        desenhar_workers(ctx.pausado ? ctx.congelado : ws);

        ImGui::SeparatorText("Soma global (leitura ingenua via UDP)");
        long long total_ref = static_cast<long long>(ctx.coletor.total());
        long long mostra   = ctx.pausado ? ctx.soma_congelada : soma_viva;
        long long deficit  = total_ref - mostra;
        bool      bate     = (mostra == total_ref);
        ImGui::TextColored(bate ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.55f, 0.3f, 1),
                           "soma = %lld   (deficit %lld)%s", mostra, deficit,
                           ctx.pausado ? "   [CONGELADO]" : "");

        // Deteccao de termino INGENUA: "todos ociosos?" so pela telemetria UDP.
        // Nao enxerga lotes em transito, entao pode declarar termino cedo demais
        // -- use o snapshot abaixo para a resposta correta.
        long long pend_viva = 0;
        bool      tem_dados = !ws.empty();
        for (const auto& [id, w] : (ctx.pausado ? ctx.congelado : ws)) {
            (void)id;
            pend_viva += w.pendentes;
        }
        bool parece_ocioso = tem_dados && pend_viva == 0;
        if (parece_ocioso)
            ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1),
                               "termino (ingenuo): parece ocioso? -- NAO confiavel (lotes em transito)");
        else
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1),
                               "termino (ingenuo): processando (pend ~ %lld)", pend_viva);
        if (ImGui::Button(ctx.pausado ? "Retomar" : "Pausar (caos)")) {
            ctx.pausado = !ctx.pausado;
            if (ctx.pausado) { ctx.congelado = ws; ctx.soma_congelada = soma_viva; }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("congela a leitura; soma quase sempre != total (tarefas em transito)");

        // Lista de workers (compartilhada por injecao e disparo de snapshot).
        std::vector<comum::NodeId> ids;
        for (const auto& w : ctx.coletor.workers()) ids.push_back(w.id);
        if (ctx.iniciador_idx >= static_cast<int>(ids.size())) ctx.iniciador_idx = 0;
        if (ctx.alvo_idx      >= static_cast<int>(ids.size())) ctx.alvo_idx      = 0;

        ImGui::SeparatorText("Injetar carga (runtime)");
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("tarefas", &ctx.qtd_injetar, 1000, 10000);
        if (ctx.qtd_injetar < 0) ctx.qtd_injetar = 0;
        ImGui::SameLine();
        ImGui::TextUnformatted("no:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        std::string alvo_atual = ids.empty() ? "-" : std::to_string(ids[ctx.alvo_idx]);
        if (ImGui::BeginCombo("##alvo", alvo_atual.c_str())) {
            for (int i = 0; i < static_cast<int>(ids.size()); ++i)
                if (ImGui::Selectable(std::to_string(ids[i]).c_str(), i == ctx.alvo_idx))
                    ctx.alvo_idx = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Injetar") && !ids.empty() && ctx.qtd_injetar > 0)
            ctx.coletor.atribuir_tarefas(ids[ctx.alvo_idx],
                                         static_cast<comum::Contagem>(ctx.qtd_injetar));
        ImGui::SameLine();
        ImGui::TextDisabled("injete e deixe assentar antes de capturar");

        ImGui::SeparatorText("Snapshot consistente (Chandy-Lamport)");

        // Botao sempre habilitado: o disparo e fire-and-forget, varios snapshots
        // podem rodar ao mesmo tempo (snapshots concorrentes).
        if (ImGui::Button("Capturar Snapshot (ordem)") && !ids.empty())
            ctx.coletor.disparar(ids[ctx.iniciador_idx]);

        ImGui::SameLine();
        ImGui::TextUnformatted("iniciador:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        std::string atual = ids.empty() ? "-" : std::to_string(ids[ctx.iniciador_idx]);
        if (ImGui::BeginCombo("##inic", atual.c_str())) {
            for (int i = 0; i < static_cast<int>(ids.size()); ++i)
                if (ImGui::Selectable(std::to_string(ids[i]).c_str(), i == ctx.iniciador_idx))
                    ctx.iniciador_idx = i;
            ImGui::EndCombo();
        }

        auto sessao     = ctx.coletor.resultados_sessao();
        auto anteriores = ctx.coletor.resultados_anteriores();
        float meia = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        desenhar_lista_snapshots("Sessao", "snap_sessao", sessao, meia);
        ImGui::SameLine();
        desenhar_lista_snapshots("Anteriores", "snap_ant", anteriores, 0.0f);
    }
    ImGui::EndChild();

    // ===================== COLUNA DIREITA =====================
    ImGui::SameLine();
    ImGui::BeginChild("dir", ImVec2(0, 0), true);
    {
        ImGui::SeparatorText("Log de eventos");
        ImGui::BeginChild("logscroll");
        for (const auto& linha : log) ImGui::TextUnformatted(linha.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f)
            ImGui::SetScrollHereY(1.0f);  // autoscroll quando ja no fim
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace monitor
