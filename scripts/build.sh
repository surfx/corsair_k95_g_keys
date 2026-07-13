#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== kill ==="
bash "$SCRIPT_DIR/kill.sh"
echo ""
echo "=== build ==="
make -C "$PROJECT_DIR"

if [ $? -eq 0 ]; then
    echo ""
    echo "Build concluido com sucesso: $PROJECT_DIR/readcorsair"
else
    echo ""
    echo "Erro no build."
    exit 1
fi
