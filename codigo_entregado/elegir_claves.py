#!/usr/bin/env python3
"""Elige claves para validar los sketches sobre la traza sin ataques (§6.1, req. 6).

Cuenta la clave (src o dst) en la primera ventana (t0, t0 + W] de la traza y
elige, de forma determinista, claves de frecuencia alta (top), media y baja.
Escribe una IP por línea seguida de su frecuencia en esa ventana, que es el
formato que acepta `sliding_sketch --query-file`.

    python elegir_claves.py traza.bin --key dst --out claves_dst.txt
"""
import argparse

import numpy as np

REC = np.dtype([("ts_us", "<u8"), ("src", "<u4"), ("dst", "<u4"), ("sport", "<u2"),
                ("dport", "<u2"), ("len", "<u2"), ("proto", "u1"), ("flags", "u1")])

# (nombre del grupo, frecuencia objetivo en la primera ventana; None = top)
GRUPOS = [("alta", None), ("media", 1000), ("media-baja", 100), ("baja", 10)]


def int2ip(v):
    v = int(v)
    return f"{v >> 24}.{(v >> 16) & 255}.{(v >> 8) & 255}.{v & 255}"


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("traza")
    p.add_argument("--key", choices=["src", "dst"], required=True)
    p.add_argument("--por-grupo", type=int, default=10)
    p.add_argument("-W", type=float, default=60.0)
    p.add_argument("--out", required=True)
    a = p.parse_args()

    r = np.memmap(a.traza, dtype=REC, mode="r")
    t0 = int(r["ts_us"][0])
    # La traza está ordenada: la primera ventana termina en el primer índice > t0 + W.
    ts = r["ts_us"][: 20_000_000]
    lo = int(np.searchsorted(ts, t0, side="right"))
    hi = int(np.searchsorted(ts, t0 + int(a.W * 1e6), side="right"))
    claves, f = np.unique(np.asarray(r[a.key][lo:hi]), return_counts=True)
    orden = np.argsort(-f, kind="stable")
    claves, f = claves[orden], f[orden]

    elegidas = []
    for nombre, objetivo in GRUPOS:
        if objetivo is None:
            idx = np.arange(a.por_grupo)
        else:
            # Las por_grupo claves más cercanas a la frecuencia objetivo.
            idx = np.argsort(np.abs(f - objetivo), kind="stable")[: a.por_grupo]
        for i in idx:
            elegidas.append((int2ip(claves[i]), int(f[i]), nombre))

    with open(a.out, "w", newline="\n") as fo:
        for ip, fi, nombre in elegidas:
            fo.write(f"{ip},{fi},{nombre}\n")
    print(f"{a.key}: {len(claves)} claves distintas en la primera ventana; "
          f"{len(elegidas)} elegidas -> {a.out}")


if __name__ == "__main__":
    main()
