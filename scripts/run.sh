#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BIN="$PROJECT_DIR/readcorsair"
PID_FILE="$PROJECT_DIR/.readcorsair.pid"

echo "=== kill ==="
bash "$SCRIPT_DIR/kill.sh"
echo ""

if [ ! -f "$BIN" ]; then
    echo "Binario nao encontrado. Buildando primeiro..."
    bash "$SCRIPT_DIR/build.sh"
    echo ""
fi

if [ ! -f "$BIN" ]; then
    echo "Erro: binario readcorsair nao encontrado apos build."
    exit 1
fi

echo "=== run ==="
echo "Iniciando readcorsair..."
echo "$$" > "$PID_FILE"
exec "$BIN" "$@"
