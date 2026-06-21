cmake_minimum_required(VERSION 3.16)
project(snapshot_distribuido CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Camadas compartilhadas (dependencia estritamente para cima):
#   comum  <-  rede  <-  snapshot  <-  entidade
#   orquestrador e monitor ficam no topo.
# ---------------------------------------------------------------------------

# Camada de transporte (Berkeley sockets) + utilitarios comuns.
add_library(rede STATIC
    src/rede/protocolo.cpp
    src/rede/socket_tcp.cpp
    src/rede/canal.cpp
    src/rede/telemetria_udp.cpp
)
target_include_directories(rede PUBLIC src)
target_link_libraries(rede PUBLIC Threads::Threads)

# Building block: Chandy-Lamport.
add_library(snapshot STATIC
    src/snapshot/chandy_lamport.cpp
)
target_include_directories(snapshot PUBLIC src)
target_link_libraries(snapshot PUBLIC rede)

# ---------------------------------------------------------------------------
# Binario 1: entidade (worker). Spawnado pelo orquestrador.
# ---------------------------------------------------------------------------
add_executable(entidade
    src/entidade/main_entidade.cpp
    src/entidade/worker.cpp
    src/entidade/work_stealing.cpp
    src/entidade/controle.cpp
)
target_link_libraries(entidade PRIVATE snapshot)

# ---------------------------------------------------------------------------
# Binario 2: orquestrador (entry point unico).
# ---------------------------------------------------------------------------
add_executable(orquestrador
    src/orquestrador/main_orquestrador.cpp
    src/orquestrador/topologia.cpp
    src/orquestrador/spawner.cpp
)
target_link_libraries(orquestrador PRIVATE rede)

# ---------------------------------------------------------------------------
# Binario 3: monitor (ImGui). Telemetria UDP + coletor de snapshots.
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

add_executable(monitor
    src/monitor/main_monitor.cpp
    src/monitor/ui_imgui.cpp
    src/monitor/receptor_telemetria.cpp
    src/monitor/coletor_snapshot.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(monitor PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends ${GLFW_INCLUDE_DIRS})
target_link_libraries(monitor PRIVATE rede ${GLFW_LIBRARIES} OpenGL::GL)
