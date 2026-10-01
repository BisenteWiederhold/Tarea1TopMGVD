#!/usr/bin/env python3
"""Figuras y tablas de la Tarea 1 a partir de los CSV de run_experimentos.sh.

Entradas en --dir (por defecto resultados/):
    exact_<exp>.csv          salida --out-query de exact_hh
    sk_<exp>_s<semilla>.csv  salida de sliding_sketch
y los gt_*.json del ataque en el directorio actual.

Salidas en --dir:
    validacion.csv / .md        error de CMS y CS sin ataque, por grupo de claves y w
    error_por_ventana.csv       error de cada ventana de J, por ataque/sketch/w/semilla
    resumen.csv / .md           MRE, memoria, latencia y falsos positivos
    delta_extremos.csv / .md    mayores incrementos/decrementos de Delta f
    fig_<ataque>.png            frecuencia exacta vs estimada (240-390 s)
    fig_delta_<ataque>.png      Delta f exacto vs CS y CMS-mediana
"""
import argparse
import json
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

D = 5
M = 6          # sub-sketches del anillo; + 1 agregado A
W_S = 60.0
CELDA = 4      # bytes por contador (uint32 en CMS, int32 en CS)

ATAQUES = {    # experimento -> archivo ground truth
    "ddos": "gt_ddos.json",
    "scan": "gt_scan.json",
    "ddos_bajo": "gt_ddos_bajo.json",
    "scan_bajo": "gt_scan_bajo.json",
}
SKETCHES = [("cms", "CMS"), ("cs", "CS")]
COLORES = {256: "#9467bd", 512: "#8c564b", 1024: "#d62728", 4096: "#ff7f0e", 16384: "#2ca02c"}


def cargar(dirn, exp, semilla):
    ex = pd.read_csv(os.path.join(dirn, f"exact_{exp}.csv"))
    sk = pd.read_csv(os.path.join(dirn, f"sk_{exp}_s{semilla}.csv"))
    df = sk.merge(ex[["win", "key", "N", "threshold", "exact_f", "exact_hh", "exact_delta"]],
                  on=["win", "key"], suffixes=("", "_ex"))
    # El anillo ya se verificó en sliding_sketch (--verify); se comprueba otra vez aquí.
    assert (df["N"] == df["N_ex"]).all(), f"N_j no coincide en {exp}, semilla {semilla}"
    assert (df["threshold"] == df["threshold_ex"]).all()
    return df.drop(columns=["N_ex", "threshold_ex"])


def tabla_md(df, floatfmt=".3g"):
    cols = list(df.columns)
    filas = ["| " + " | ".join(map(str, cols)) + " |", "|" + "---|" * len(cols)]
    for _, r in df.iterrows():
        celdas = []
        for c in cols:
            v = r[c]
            if isinstance(v, float) and np.isnan(v):
                celdas.append("")
            elif isinstance(v, float) and v.is_integer() and abs(v) >= 10:
                celdas.append(str(int(v)))
            elif isinstance(v, float):
                celdas.append(format(v, floatfmt))
            else:
                celdas.append(str(v))
        filas.append("| " + " | ".join(celdas) + " |")
    return "\n".join(filas) + "\n"


def guardar(df, dirn, nombre, floatfmt=".3g"):
    df.to_csv(os.path.join(dirn, nombre + ".csv"), index=False)
    with open(os.path.join(dirn, nombre + ".md"), "w", encoding="utf-8") as f:
        f.write(tabla_md(df, floatfmt))


# ------------------------------------------------------- validación sin ataque

def validacion(dirn, semillas):
    filas = []
    for exp in ("base_dst", "base_src"):
        grupos = pd.read_csv(f"claves_{exp.split('_')[1]}.txt", header=None,
                             names=["key", "f_ventana0", "grupo"])
        for s in semillas:
            df = cargar(dirn, exp, s).merge(grupos[["key", "grupo"]], on="key")
            for (w, grupo), g in df.groupby(["w", "grupo"]):
                for col, nombre in SKETCHES:
                    err = g[f"{col}_f"] - g["exact_f"]
                    pos = g["exact_f"] > 0
                    filas.append(dict(clave=exp.split("_")[1], grupo=grupo, w=w, sketch=nombre,
                                      semilla=s, ea_medio=err.abs().mean(),
                                      er_medio=(err[pos].abs() / g.loc[pos, "exact_f"]).mean(),
                                      sesgo=err.mean()))
    v = pd.DataFrame(filas)
    orden_g = {"alta": 0, "media": 1, "media-baja": 2, "baja": 3}
    res = (v.groupby(["clave", "grupo", "w", "sketch"], as_index=False)
             [["ea_medio", "er_medio", "sesgo"]].mean())
    res["_o"] = res["grupo"].map(orden_g)
    res = res.sort_values(["clave", "_o", "w", "sketch"]).drop(columns="_o")
    guardar(res, dirn, "validacion")
    return res


# ------------------------------------------------------------ ataques

def analizar_ataque(dirn, exp, semillas):
    gt = json.load(open(ATAQUES[exp], encoding="utf-8"))
    ini, fin = gt["ventana_ataque_rel_s"]
    dfs = {s: cargar(dirn, exp, s) for s in semillas}
    resumen, por_ventana = [], []
    for s, df in dfs.items():
        t = df["t_rel_s"]
        # J: evaluaciones posteriores al inicio y anteriores a que el último
        # paquete del ataque salga de la ventana, con f_j(x) > 0.
        enJ = (t > ini) & (t < fin + W_S) & (df["exact_f"] > 0)
        ex = df[df["w"] == df["w"].iloc[0]]
        lat_ex = ex.loc[ex["exact_hh"] == 1, "t_rel_s"].min() - ini
        for w, g in df.groupby("w"):
            gJ = g[enJ.loc[g.index]]
            for col, nombre in SKETCHES:
                er = (gJ[f"{col}_f"] - gJ["exact_f"]).abs() / gJ["exact_f"]
                for _, r in gJ.assign(er=er).iterrows():
                    por_ventana.append(dict(ataque=exp, sketch=nombre, w=w, semilla=s,
                                            t_rel_s=r["t_rel_s"], exact_f=r["exact_f"],
                                            estimado=r[f"{col}_f"], er=r["er"]))
                hh = g[f"{col}_hh"] == 1
                fp = (hh & (g["exact_hh"] == 0))
                fn = (~hh & (g["exact_hh"] == 1))
                resumen.append(dict(
                    ataque=exp, sketch=nombre, w=w, semilla=s, J=len(gJ), MRE=er.mean(),
                    memoria_KB=M * D * w * CELDA / 1024 + D * w * CELDA / 1024,
                    latencia_s=g.loc[hh, "t_rel_s"].min() - ini, latencia_exacta_s=lat_ex,
                    FP=int(fp.sum()), FN=int(fn.sum()),
                    t_FP=" ".join(f"{x:.0f}" for x in g.loc[fp, "t_rel_s"]),
                    t_FN=" ".join(f"{x:.0f}" for x in g.loc[fn, "t_rel_s"])))
    figura_frecuencia(dirn, exp, dfs[semillas[0]], gt, semillas[0])
    figura_delta(dirn, exp, dfs[semillas[0]], gt, semillas[0])
    extremos = delta_extremos(exp, dfs[semillas[0]])
    return resumen, por_ventana, extremos


def nombre_ataque(exp, gt):
    tipo = "DDoS" if exp.startswith("ddos") else "Scan"
    a = gt["ataque"]
    ip = a.get("victima") or a.get("atacante")
    clave = "IP destino" if tipo == "DDoS" else "IP origen"
    return f"{tipo} ({clave} {ip}, {gt['pps_nominal']:.0f} pps)"


def figura_frecuencia(dirn, exp, df, gt, semilla):
    ini, fin = gt["ventana_ataque_rel_s"]
    lo, hi = ini - 60, fin + 60
    d = df[(df["t_rel_s"] >= lo) & (df["t_rel_s"] <= hi)]
    ex = d[d["w"] == d["w"].iloc[0]]
    fig, (ax, ax2) = plt.subplots(2, 1, figsize=(8, 7), sharex=True,
                                  gridspec_kw={"height_ratios": [3, 2]})
    ax.plot(ex["t_rel_s"], ex["exact_f"], "k-", lw=2.5, label="exacto", zorder=5)
    ax.plot(ex["t_rel_s"], ex["threshold"], "k:", lw=1.2, label=r"umbral $\lceil\varphi N_j\rceil$")
    ax2.axhline(0, color="k", lw=2)
    for w, g in d.groupby("w"):
        c = COLORES.get(w)
        ax.plot(g["t_rel_s"], g["cms_f"], "--o", color=c, ms=3, lw=1, label=f"CMS w={w}")
        ax.plot(g["t_rel_s"], g["cs_f"], "-s", color=c, ms=3, lw=1, alpha=.7, label=f"CS w={w}")
        ax2.plot(g["t_rel_s"], g["cms_f"] - g["exact_f"], "--o", color=c, ms=3, lw=1)
        ax2.plot(g["t_rel_s"], g["cs_f"] - g["exact_f"], "-s", color=c, ms=3, lw=1, alpha=.7)
    for a in (ax, ax2):
        a.axvspan(ini, fin, color="grey", alpha=.12)
        a.grid(alpha=.3)
    ax.set_ylabel(r"$f_j(x)$ [paquetes en la ventana]")
    ax2.set_ylabel(r"error $\hat f_j(x) - f_j(x)$")
    ax2.set_xlabel(r"$\tau_j - t_0$ [s]")
    ax.set_title(f"{nombre_ataque(exp, gt)}, d={D}, semilla hash {semilla}")
    ax.legend(fontsize=7, ncol=2, loc="upper left")
    fig.tight_layout()
    fig.savefig(os.path.join(dirn, f"fig_{exp}.png"), dpi=150)
    plt.close(fig)


def figura_delta(dirn, exp, df, gt, semilla):
    """Arriba: Delta f exacto y ambos estimadores con el w más chico (forma del
    cambio). Abajo: error de cada estimador para todos los w, donde se ven las
    diferencias que a la escala del ataque quedan tapadas."""
    ini, fin = gt["ventana_ataque_rel_s"]
    lo, hi = ini - 60, fin + W_S + 30
    d = df[(df["t_rel_s"] >= lo) & (df["t_rel_s"] <= hi)].dropna(subset=["exact_delta"])
    ws = sorted(d["w"].unique())
    fig, axes = plt.subplots(3, 1, figsize=(8, 8.5), sharex=True,
                             gridspec_kw={"height_ratios": [2, 1.4, 1.4]})
    g = d[d["w"] == ws[0]]
    ax = axes[0]
    ax.plot(g["t_rel_s"], g["exact_delta"], "k-", lw=2.5, label=r"$\Delta f$ exacto")
    ax.plot(g["t_rel_s"], g["cs_delta"], "-s", color="#1f77b4", ms=4,
            label=rf"$\widehat{{\Delta f}}$ CS (w={ws[0]})")
    ax.plot(g["t_rel_s"], g["cms_med_delta"], "--o", color="#d62728", ms=4,
            label=rf"$\widehat{{\Delta f}}$ CMS-mediana (w={ws[0]})")
    ax.set_ylabel(r"$\Delta f_j(x)$")
    ax.legend(fontsize=8)
    ax.set_title(f"{nombre_ataque(exp, gt)}: cambio entre ventanas, semilla {semilla}",
                 fontsize=10, loc="left")
    for ax, col, nombre in ((axes[1], "cs_delta", "CS"), (axes[2], "cms_med_delta", "CMS-mediana")):
        for w in ws:
            gw = d[d["w"] == w]
            ax.plot(gw["t_rel_s"], gw[col] - gw["exact_delta"], "-o", color=COLORES.get(w),
                    ms=3, lw=1, label=f"w={w}")
        ax.set_ylabel(f"error {nombre}\n" + r"$\widehat{\Delta f} - \Delta f$")
        ax.legend(fontsize=7, ncol=len(ws), loc="upper left")
    lim = max(abs(a) for ax in axes[1:] for a in ax.get_ylim())
    for ax in axes[1:]:
        ax.set_ylim(-lim, lim)
    for ax in axes:
        ax.axvspan(ini, fin, color="grey", alpha=.12)
        ax.axhline(0, color="grey", lw=.8)
        ax.grid(alpha=.3)
    axes[-1].set_xlabel(r"$\tau_j - t_0$ [s]")
    fig.tight_layout()
    fig.savefig(os.path.join(dirn, f"fig_delta_{exp}.png"), dpi=150)
    plt.close(fig)


def delta_extremos(exp, df):
    filas = []
    d = df.dropna(subset=["exact_delta"])
    for w, g in d.groupby("w"):
        for tipo, idx in (("mayor incremento", g["exact_delta"].idxmax()),
                          ("mayor decremento", g["exact_delta"].idxmin())):
            r = g.loc[idx]
            filas.append(dict(ataque=exp, w=w, tipo=tipo, t_rel_s=r["t_rel_s"],
                              exacto=int(r["exact_delta"]), CS=int(r["cs_delta"]),
                              CMS_med=int(r["cms_med_delta"]),
                              err_CS=int(r["cs_delta"] - r["exact_delta"]),
                              err_CMS_med=int(r["cms_med_delta"] - r["exact_delta"])))
        # Error medio absoluto de Delta f en toda la traza y fuera del ataque.
        filas.append(dict(ataque=exp, w=w, tipo="MAE toda la traza", t_rel_s=np.nan,
                          exacto=np.nan, CS=np.nan, CMS_med=np.nan,
                          err_CS=(g["cs_delta"] - g["exact_delta"]).abs().mean(),
                          err_CMS_med=(g["cms_med_delta"] - g["exact_delta"]).abs().mean()))
    return filas


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--dir", default="resultados")
    p.add_argument("--seeds", nargs="+", type=int, default=[42, 7, 1234])
    a = p.parse_args()

    val = validacion(a.dir, a.seeds)
    print("== validación sin ataque (promedio sobre semillas) ==")
    print(val.to_string(index=False))

    res, pv, ext = [], [], []
    for exp in ATAQUES:
        if not os.path.exists(os.path.join(a.dir, f"exact_{exp}.csv")):
            print(f"(se omite {exp}: no hay resultados)")
            continue
        r, v, e = analizar_ataque(a.dir, exp, a.seeds)
        res += r; pv += v; ext += e

    pv = pd.DataFrame(pv)
    pv.to_csv(os.path.join(a.dir, "error_por_ventana.csv"), index=False)
    res = pd.DataFrame(res)
    res.to_csv(os.path.join(a.dir, "resumen_por_semilla.csv"), index=False)

    # Resumen: MRE de la primera semilla, media y desviación entre semillas;
    # latencia y falsos positivos de la primera semilla (y si cambian entre semillas).
    s0 = a.seeds[0]
    agg = (res.groupby(["ataque", "sketch", "w"], sort=False)
              .agg(MRE_media=("MRE", "mean"), MRE_std=("MRE", "std"),
                   lat_min=("latencia_s", "min"), lat_max=("latencia_s", "max"),
                   FP_total=("FP", "sum")).reset_index())
    base = res[res["semilla"] == s0][["ataque", "sketch", "w", "J", "MRE", "memoria_KB",
                                       "latencia_s", "latencia_exacta_s", "FP", "t_FP", "FN"]]
    tabla = base.merge(agg, on=["ataque", "sketch", "w"])
    tabla = tabla.rename(columns={"MRE": f"MRE_s{s0}"})
    guardar(tabla, a.dir, "resumen")
    print("\n== resumen ==")
    print(tabla.to_string(index=False))

    ext = pd.DataFrame(ext)
    guardar(ext, a.dir, "delta_extremos", floatfmt=".4g")
    print("\n== Delta f: extremos ==")
    print(ext.to_string(index=False))


if __name__ == "__main__":
    main()
