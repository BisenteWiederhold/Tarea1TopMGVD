#!/usr/bin/env bash
# Reproduce todos los experimentos de la Tarea 1:
#   1. compila sliding_sketch (y exact_hh si se puede);
#   2. prepara las trazas (preparar_trazas.sh) y, además, una versión de baja
#      intensidad de cada ataque (--pps $PPS_BAJO) para que el error de los
#      sketches sea visible (§2 del enunciado);
#   3. corre exact_hh: validación sin ataque (claves de frecuencia alta, media
#      y baja) y la clave de cada ataque;
#   4. corre sliding_sketch (CMS y CS, d = 5, w = 256, 1024, 4096, 16384) con varias
#      semillas de hash, verificando N_j contra exact_hh en cada corrida;
#   5. genera figuras y tablas con analisis.py.
#
# Es idempotente: omite los resultados que ya existen. FORCE=1 rehace todo.
# En Windows se ejecuta desde Git Bash / MSYS2; exact_hh corre en WSL.
set -euo pipefail
cd "$(dirname "$0")"

PYTHON=${PYTHON:-python}
D=5
WIDTHS=256,1024,4096,16384
SEEDS=${SEEDS:-"42 7 1234"}
ATK_SEED=42
PPS_BAJO=${PPS_BAJO:-3000}
OUT=resultados
FORCE=${FORCE:-0}
mkdir -p "$OUT"

falta() { [ "$FORCE" = 1 ] || [ ! -s "$1" ]; }

echo "[1/5] Compilando"
g++ -O2 -march=native -std=c++17 -Wall -Wextra -static -o sliding_sketch sliding_sketch.cpp
# exact_hh usa mmap: compila en Linux/WSL, no con MinGW.
if g++ -O2 -march=native -std=c++17 -o exact_hh exact_hh.cpp 2>/dev/null; then
    EXACT=(./exact_hh)
elif command -v wsl >/dev/null; then
    wsl g++ -O2 -march=native -std=c++17 -o exact_hh exact_hh.cpp
    EXACT=(wsl ./exact_hh)
else
    echo "No se pudo compilar exact_hh (ni nativo ni en WSL)" >&2; exit 1
fi

echo "[2/5] Trazas"
bash preparar_trazas.sh
falta traza_ddos_bajo.bin && "$PYTHON" inject_attack.py ddos --base traza.bin \
    --out traza_ddos_bajo.bin --gt gt_ddos_bajo.json --start 300 --duration 30 \
    --pps "$PPS_BAJO" --sources 4000 --seed $ATK_SEED --victim 163.210.30.13
falta traza_scan_bajo.bin && "$PYTHON" inject_attack.py scan --base traza.bin \
    --out traza_scan_bajo.bin --gt gt_scan_bajo.json --start 300 --duration 30 \
    --pps "$PPS_BAJO" --dst-count 60000 --seed $ATK_SEED

# Experimentos: nombre traza clave consulta(s)
VICTIMA=163.210.30.13
ATACANTE=198.18.0.7
EXPERIMENTOS=(
    "base_dst traza.bin dst @claves_dst.txt"
    "base_src traza.bin src @claves_src.txt"
    "ddos traza_ddos.bin dst $VICTIMA"
    "scan traza_scan.bin src $ATACANTE"
    "ddos_bajo traza_ddos_bajo.bin dst $VICTIMA"
    "scan_bajo traza_scan_bajo.bin src $ATACANTE"
)

echo "[3/5] Referencia exacta (exact_hh)"
falta claves_dst.txt && "$PYTHON" elegir_claves.py traza.bin --key dst --out claves_dst.txt
falta claves_src.txt && "$PYTHON" elegir_claves.py traza.bin --key src --out claves_src.txt
for e in "${EXPERIMENTOS[@]}"; do
    read -r nombre traza clave consulta <<<"$e"
    salida="$OUT/exact_$nombre.csv"
    falta "$salida" || { echo "  $salida ya existe"; continue; }
    if [[ $consulta == @* ]]; then
        args=()
        while IFS=, read -r ip _; do args+=(--query "$ip"); done < "${consulta#@}"
    else
        args=(--query "$consulta")
    fi
    echo "  $nombre"
    "${EXACT[@]}" "$traza" --key "$clave" -W 60 --delta 10 --phi 0.01 \
        "${args[@]}" --out-query "$salida" > "$OUT/exact_$nombre.log"
done

echo "[4/5] Sketches (d=$D, w=$WIDTHS, semillas: $SEEDS)"
for e in "${EXPERIMENTOS[@]}"; do
    read -r nombre traza clave consulta <<<"$e"
    if [[ $consulta == @* ]]; then q=(--query-file "${consulta#@}"); else q=(--query "$consulta"); fi
    for s in $SEEDS; do
        salida="$OUT/sk_${nombre}_s$s.csv"
        falta "$salida" || { echo "  $salida ya existe"; continue; }
        echo "  $nombre, semilla $s"
        # --verify aborta (código 3) si N_j difiere de exact_hh en alguna ventana.
        ./sliding_sketch "$traza" --key "$clave" "${q[@]}" -d $D -w $WIDTHS --seed "$s" \
            --out "$salida" --verify "$OUT/exact_$nombre.csv" 2> "$OUT/sk_${nombre}_s$s.log"
        grep "autoverificación" "$OUT/sk_${nombre}_s$s.log"
    done
done

echo "[5/5] Figuras y tablas"
"$PYTHON" analisis.py --dir "$OUT" --seeds $SEEDS
echo "Listo: resultados en $OUT/"
