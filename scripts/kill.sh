#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PID_FILE="$PROJECT_DIR/.readcorsair.pid"

if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE")
    if kill -0 "$PID" 2>/dev/null; then
        echo "Matendo readcorsair (PID $PID)..."
        kill "$PID" 2>/dev/null
        sleep 0.5
        if kill -0 "$PID" 2>/dev/null; then
            kill -9 "$PID" 2>/dev/null
        fi
        echo "Processo finalizado."
    else
        echo "Processo $PID nao esta rodando."
    fi
    rm -f "$PID_FILE"
else
    PIDS=$(pgrep -x readcorsair 2>/dev/null)
    if [ -n "$PIDS" ]; then
        echo "Matendo processos readcorsair: $PIDS"
        echo "$PIDS" | xargs kill 2>/dev/null
        sleep 0.5
        PIDS=$(pgrep -x readcorsair 2>/dev/null)
        if [ -n "$PIDS" ]; then
            echo "$PIDS" | xargs kill -9 2>/dev/null
        fi
        echo "Processos finalizados."
    else
        echo "Nenhum processo readcorsair rodando."
    fi
fi
