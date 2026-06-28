#!/usr/bin/env bash
# Compila (se preciso), sobe o cluster (orquestrador + workers) e abre o Monitor.
# Fechar a janela do Monitor -- ou Ctrl-C -- encerra TODOS os processos.
#
# As tarefas sao injetadas em runtime pelo Monitor (nao ha mais carga fixa).
#
# Variaveis de ambiente (opcionais):
#   WORKERS=4  TOPOLOGIA=ring   (ex.: WORKERS=6 ./run.sh)
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

WORKERS="${WORKERS:-12}"
TOPOLOGIA="${TOPOLOGIA:-mesh}"

ORQ="$DIR/build/bin/orquestrador"
MON="$DIR/build/bin/monitor"

# 1) Build (incremental).
[ -d build ] || cmake -S . -B build
cmake --build build -j"$(nproc)"

# Encerramento limpo: derruba orquestrador (que repassa SIGTERM aos workers).
ORQ_PID=""
cleanup() {
    echo
    echo ">> encerrando..."
    [ -n "$ORQ_PID" ] && kill -INT "$ORQ_PID" 2>/dev/null || true
    pkill -TERM -f "$DIR/build/bin/entidade" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

[ -z "${DISPLAY:-}" ] && echo "[aviso] DISPLAY nao definido; o Monitor precisa de um display grafico."

# 2) Orquestrador: spawna os workers e escreve cluster.json.
echo ">> orquestrador: $WORKERS workers, topologia $TOPOLOGIA (injete tarefas no Monitor)"
"$ORQ" --workers "$WORKERS" --topologia "$TOPOLOGIA" &
ORQ_PID=$!

# Espera o cluster.json e os canais subirem.
for _ in $(seq 1 50); do [ -f cluster.json ] && break; sleep 0.1; done
sleep 1

# 3) Monitor em primeiro plano (fechar a janela encerra o demo).
echo ">> monitor (feche a janela para encerrar tudo)"
"$MON" cluster.json
