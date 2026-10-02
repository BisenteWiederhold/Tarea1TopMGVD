#!/usr/bin/env bash
# Prepara el entorno y las trazas de trabajo:
#   traza.bin       (base, sin ataques)
#   traza_ddos.bin  + gt_ddos.json
#   traza_scan.bin  + gt_scan.json
# Es idempotente: omite los pasos cuyo resultado ya existe. Si las tres trazas
# ya están (por ejemplo, copiadas desde otro PC), no descarga nada.
#
# Requisitos que el script NO instala: g++ (MSYS2 UCRT64 en Windows), Python 3,
# curl/gzip y, en Windows, WSL con g++ para exact_hh:
#   wsl -u root -e sh -c "apt update && apt install -y g++"
set -euo pipefail
cd "$(dirname "$0")"

URL=https://mawi.wide.ad.jp/mawi/samplepoint-F/2018/201812031400.pcap.gz
PCAP=201812031400.pcap.gz
PCAP_SOURCE=${PCAP_SOURCE:-}
SEED=42
PYTHON=${PYTHON:-python}

echo "[1/5] Verificando requisitos"
faltan=0
for cmd in g++ "$PYTHON" curl gzip zcat; do
    command -v "$cmd" >/dev/null || { echo "  FALTA: $cmd"; faltan=1; }
done
[ "$faltan" = 0 ] || { echo "Instale lo que falta y vuelva a ejecutar." >&2; exit 1; }
"$PYTHON" -m pip install -q -r requirements.txt

echo "[2/5] Compilando herramientas"
g++ -O2 -march=native -std=c++17 -o pcap2bin pcap2bin.cpp
# exact_hh usa mmap (sys/mman.h): compila en Linux/WSL, no con MinGW/UCRT.
# No es necesario para generar las trazas, así que su fallo no detiene el script.
if g++ -O2 -march=native -std=c++17 -o exact_hh exact_hh.cpp 2>/dev/null; then
    echo "  exact_hh compilado (ejecutar con ./exact_hh)"
elif command -v wsl >/dev/null && wsl g++ -O2 -march=native -std=c++17 -o exact_hh exact_hh.cpp; then
    echo "  exact_hh compilado en WSL (ejecutar con: wsl ./exact_hh ...)"
else
    echo "  AVISO: exact_hh no compiló. Instale g++ en WSL:"
    echo "    wsl -u root -e sh -c \"apt update && apt install -y g++\""
fi

if [ -s traza.bin ]; then
    echo "[3/5] traza.bin ya existe, se omite la descarga"
else
    if [ -n "$PCAP_SOURCE" ]; then
        [ -s "$PCAP_SOURCE" ] || { echo "No existe PCAP_SOURCE: $PCAP_SOURCE" >&2; exit 1; }
        echo "[3/5] Usando PCAP_SOURCE=$PCAP_SOURCE"
    elif [ -s "$PCAP" ]; then
        PCAP_SOURCE="$PCAP"
    elif [ -s "${HOME}/Downloads/$PCAP" ]; then
        PCAP_SOURCE="${HOME}/Downloads/$PCAP"
        echo "[3/5] Usando la traza de Descargas: $PCAP_SOURCE"
    else
        PCAP_SOURCE=$PCAP
        echo "[3/5] Descargando $URL"
        # -C - reanuda una descarga interrumpida; el servidor de MAWI suele cortar la
        # conexión en descargas largas, así que se reintenta hasta completar el archivo.
        SIZE=$(curl -sIL "$URL" | grep -i '^content-length:' | tail -1 | tr -dc '0-9')
        for intento in $(seq 1 30); do
            [ -f "$PCAP_SOURCE" ] && [ "$(stat -c %s "$PCAP_SOURCE")" = "$SIZE" ] && break
            curl -sS -L -C - -o "$PCAP_SOURCE" "$URL" || echo "  intento $intento interrumpido, reanudando..."
        done
        [ "$(stat -c %s "$PCAP_SOURCE")" = "$SIZE" ] || { echo "Descarga incompleta" >&2; exit 1; }
    fi
    gzip -t "$PCAP_SOURCE"

    echo "      Convirtiendo pcap a traza.bin"
    zcat "$PCAP_SOURCE" | ./pcap2bin > traza.bin.tmp
    mv traza.bin.tmp traza.bin
fi

echo "[4/5] Inyectando ataques (semilla $SEED)"
# La víctima se fija explícitamente: en modo auto, inject_attack.py la elige con
# np.argsort (no estable) y con empates distintas versiones de numpy eligen otra IP.
[ -s traza_ddos.bin ] || "$PYTHON" inject_attack.py ddos --base traza.bin --out traza_ddos.bin \
    --gt gt_ddos.json --start 300 --duration 30 --pps 10000 --sources 4000 --seed $SEED \
    --victim 163.210.30.13
[ -s traza_scan.bin ] || "$PYTHON" inject_attack.py scan --base traza.bin --out traza_scan.bin \
    --gt gt_scan.json --start 300 --duration 30 --pps 8000 --dst-count 60000 --seed $SEED

echo "[5/5] Verificando tamaños (24 bytes por registro)"
verificar() {
    local n=$(( $(stat -c %s "$1") / 24 ))
    if [ "$n" = "$2" ]; then echo "  OK   $1: $n registros"
    else echo "  MAL  $1: $n registros, se esperaban $2" >&2; exit 1; fi
}
verificar traza.bin      123383971
verificar traza_ddos.bin 123683971
verificar traza_scan.bin 123623971
echo "Listo."
