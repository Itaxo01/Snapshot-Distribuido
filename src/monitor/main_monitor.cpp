// Entry point do Monitor (ImGui + GLFW + OpenGL3).
//
// Le cluster.json (escrito pelo orquestrador), abre o receptor de telemetria UDP
// e o coletor de snapshots, e roda o laco de render.
//
// Uso: monitor [cluster.json]
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "comum/json.hpp"
#include "comum/parametros.hpp"
#include "comum/tipos.hpp"
#include "monitor/coletor_snapshot.hpp"
#include "monitor/receptor_telemetria.hpp"
#include "monitor/ui_imgui.hpp"

namespace {

struct ClusterInfo {
    std::string                       host;
    comum::Porta                      telemetria = 0;
    std::vector<monitor::EndpointCtrl> endpoints;
};

ClusterInfo carregar_cluster(const std::string& caminho) {
    std::ifstream in(caminho);
    if (!in) throw std::runtime_error("nao foi possivel abrir " + caminho);
    std::stringstream ss;
    ss << in.rdbuf();
    auto v = comum::json::parse(ss.str());
    if (!v) throw std::runtime_error("cluster.json invalido");

    ClusterInfo c;
    c.host       = v->campo("host") ? v->campo("host")->txt : "127.0.0.1";
    c.telemetria = static_cast<comum::Porta>(v->campo("telemetria")->inteiro());
    for (auto& w : v->campo("workers")->arr) {
        comum::NodeId id    = static_cast<comum::NodeId>(w.campo("id")->inteiro());
        comum::Porta  ctrl  = static_cast<comum::Porta>(w.campo("controle")->inteiro());
        c.endpoints.push_back({id, {c.host, ctrl}});
    }
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    std::string caminho = argc > 1 ? argv[1] : "cluster.json";
    ClusterInfo cluster;
    try {
        cluster = carregar_cluster(caminho);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "monitor: %s\n", e.what());
        return 1;
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "monitor: falha ao iniciar GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // Abre maximizada (preenche a area de trabalho, mantendo barra de titulo e
    // a barra de tarefas do SO). UI_JANELA_* vira o tamanho "restaurado".
    if (comum::parametro::UI_MAXIMIZADA) glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    GLFWwindow* janela = glfwCreateWindow(comum::parametro::UI_JANELA_LARGURA,
                                          comum::parametro::UI_JANELA_ALTURA,
                                          "Snapshot Distribuido - Monitor", nullptr, nullptr);
    if (!janela) {
        std::fprintf(stderr, "monitor: falha ao criar janela\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(janela);
    glfwSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Fonte/UI maiores para apresentacao. Ajuste UI_ESCALA (comum/parametros.hpp).
    const float  ESCALA = comum::parametro::UI_ESCALA;
    ImGuiIO&     io = ImGui::GetIO();
    ImFontConfig fonte_cfg;
    fonte_cfg.SizePixels = 13.0f * ESCALA;   // padrao do ImGui e 13px
    io.Fonts->AddFontDefault(&fonte_cfg);
    ImGui::GetStyle().ScaleAllSizes(ESCALA); // paddings, barras, etc. acompanham

    ImGui_ImplGlfw_InitForOpenGL(janela, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Receptor de telemetria (UDP) e coletor de snapshots (controle TCP).
    monitor::EstadoCluster      estado;
    monitor::ReceptorTelemetria receptor(cluster.telemetria, estado);
    receptor.iniciar();

    monitor::Coletor coletor(cluster.endpoints);
    coletor.conectar();  // conecta nas portas de controle (com retry)
    coletor.iniciar();

    monitor::ContextoUI ctx{estado, coletor,
                            std::to_string(cluster.endpoints.size()) + " workers"};

    while (!glfwWindowShouldClose(janela)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        monitor::desenhar(ctx);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(janela, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(janela);
    }

    coletor.parar();
    receptor.parar();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(janela);
    glfwTerminate();
    return 0;
}
