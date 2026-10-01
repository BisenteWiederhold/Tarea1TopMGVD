// sliding_sketch.cpp -- Count-Min Sketch y CountSketch sobre una ventana
// deslizante de W = m*p segundos, mantenida con un anillo de m sub-sketches y
// un sketch agregado A (Tarea 1, Tópicos en Grandes Volúmenes de Datos 2026).
//
// Ambos sketches comparten la misma estructura de ventana (SlidingWindow<S>):
// sólo cambian el signo de la actualización y el estimador. Se calculan en una
// sola pasada sobre la traza y, si se piden varios anchos (-w 1024,4096,16384),
// también todos los anchos en la misma pasada.
//
// Compilación:
//     g++ -O2 -march=native -std=c++17 -o sliding_sketch sliding_sketch.cpp
//
// Uso típico:
//     ./sliding_sketch traza_ddos.bin --key dst --query 163.210.30.13
//         -d 5 -w 1024,4096,16384 --seed 42 --out sk_ddos.csv
//         --verify exact_ddos.csv
//
// Convención temporal (idéntica a exact_hh):
//   - t0 es la marca del primer paquete; la subventana q (q >= 1) cubre
//     (t0 + (q-1)p, t0 + q p] y ocupa la ranura (q-1) mod m.
//   - Los paquetes con ts == t0 no pertenecen a ninguna ventana evaluada
//     (exact_hh tampoco los cuenta: su ventana 0 es (t0, t0 + W]).
//   - Las evaluaciones ocurren en tau_j = t0 + W + j p mientras tau_j <= t_fin,
//     y en tau_j el agregado contiene las subventanas j+1, ..., j+m.
//   - Todo se calcula con enteros en microsegundos.
//
// Delta f_j(x): al expirar la subventana q-m se inicializa dA = -S_{q-m}
// (antes de limpiar la ranura) y al evaluar se suma S_q, de modo que
// dA = S_q - S_{q-m} = A_j - A_{j-1} sin guardar una copia de A_{j-1}.

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct Record {
    uint64_t ts_us;
    uint32_t src, dst;
    uint16_t sport, dport, len;
    uint8_t proto, flags;
};
#pragma pack(pop)
static_assert(sizeof(Record) == 24, "el registro debe ocupar 24 bytes");

// ------------------------------------------------------------------- hash
//
// Familia de Carter-Wegman h(x) = (a x + b) mod P con P = 2^61 - 1, que es
// 2-universal. La posición en la fila es h(x) mod w. El signo de CountSketch
// usa una función independiente de la misma familia (otros a, b) y toma su bit
// menos significativo: como h(x) es casi uniforme en [0, P), ese bit es
// insesgado salvo un término O(1/P), y los signos de dos claves distintas son
// (casi) independientes por pares, que es lo que exige el análisis de CS.

static const uint64_t P61 = (1ull << 61) - 1;

static inline uint64_t cw_hash(uint64_t a, uint64_t b, uint32_t x) {
    unsigned __int128 z = (unsigned __int128)a * x + b;
    uint64_t r = (uint64_t)(z & P61) + (uint64_t)(z >> 61);
    if (r >= P61) r -= P61;
    return r;
}

struct Hashes {
    int d;
    std::vector<uint64_t> pa, pb, sa, sb;  // posición y signo, una por fila

    Hashes(int d_, uint64_t seed) : d(d_) {
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<uint64_t> da(1, P61 - 1), db(0, P61 - 1);
        for (int i = 0; i < d; ++i) {
            pa.push_back(da(rng)); pb.push_back(db(rng));
            sa.push_back(da(rng)); sb.push_back(db(rng));
        }
    }
    uint64_t pos(int i, uint32_t x) const { return cw_hash(pa[i], pb[i], x); }
    int sign(int i, uint32_t x) const { return (cw_hash(sa[i], sb[i], x) & 1) ? 1 : -1; }
};

static int64_t median(std::vector<int64_t> &v) {
    size_t n = v.size(), k = n / 2;
    std::nth_element(v.begin(), v.begin() + k, v.end());
    if (n % 2) return v[k];
    int64_t hi = v[k];
    int64_t lo = *std::max_element(v.begin(), v.begin() + k);
    return (lo + hi) / 2;
}

// --------------------------------------------------------------- sketches
//
// Cada sketch define el tipo de contador, el signo de la actualización y los
// estimadores. La ventana deslizante no sabe nada más de ellos.

struct CountMin {
    typedef uint32_t Cell;
    static int sign(int s) { (void)s; return 1; }
    // Estimador estándar: mínimo sobre las filas (sobreestima).
    static int64_t estimate(const Cell *A, int d, int w, const std::vector<uint32_t> &col,
                            const std::vector<int> &) {
        int64_t best = INT64_MAX;
        for (int i = 0; i < d; ++i) best = std::min<int64_t>(best, A[(size_t)i * w + col[i]]);
        return best;
    }
    // CMS-mediana sobre el vector firmado dA (variante experimental, §6.3).
    static int64_t estimate_delta(const int32_t *dA, int d, int w,
                                  const std::vector<uint32_t> &col, const std::vector<int> &) {
        std::vector<int64_t> v(d);
        for (int i = 0; i < d; ++i) v[i] = dA[(size_t)i * w + col[i]];
        return median(v);
    }
};

struct CountSketch {
    typedef int32_t Cell;
    static int sign(int s) { return s; }
    // Mediana de s_i(x) A[i][h_i(x)]; sin truncar (el truncamiento a 0 se hace
    // al reportar la frecuencia).
    static int64_t estimate(const Cell *A, int d, int w, const std::vector<uint32_t> &col,
                            const std::vector<int> &sg) {
        std::vector<int64_t> v(d);
        for (int i = 0; i < d; ++i) v[i] = (int64_t)sg[i] * A[(size_t)i * w + col[i]];
        return median(v);
    }
    static int64_t estimate_delta(const int32_t *dA, int d, int w,
                                  const std::vector<uint32_t> &col, const std::vector<int> &sg) {
        std::vector<int64_t> v(d);
        for (int i = 0; i < d; ++i) v[i] = (int64_t)sg[i] * dA[(size_t)i * w + col[i]];
        return median(v);
    }
};

// ------------------------------------------------------ ventana deslizante

template <class S>
class SlidingWindow {
public:
    typedef typename S::Cell Cell;

    SlidingWindow(int m, int d, int w)
        : m_(m), d_(d), w_(w), cells_((size_t)d * w),
          ring_((size_t)m * d * w, 0), A_(cells_, 0), dA_(cells_, 0) {}

    // Un paquete de la subventana que ocupa `slot`: se actualizan a la vez el
    // sub-sketch actual y el agregado.
    inline void add(int slot, const uint32_t *col, const int *sg) {
        Cell *s = &ring_[(size_t)slot * cells_];
        for (int i = 0; i < d_; ++i) {
            size_t k = (size_t)i * w_ + col[i];
            Cell c = (Cell)S::sign(sg[i]);
            s[k] += c;
            A_[k] += c;
        }
    }

    // Expira la subventana guardada en `slot`: dA = -S_viejo, A -= S_viejo y
    // la ranura queda en cero para reutilizarla. Toca d*w contadores por arreglo.
    void expire(int slot) {
        Cell *s = &ring_[(size_t)slot * cells_];
        for (size_t k = 0; k < cells_; ++k) {
            dA_[k] = -(int32_t)s[k];
            A_[k] -= s[k];
            s[k] = 0;
        }
    }

    // Completa dA = S_nuevo - S_viejo una vez cargada la subventana nueva.
    void close_delta(int slot) {
        const Cell *s = &ring_[(size_t)slot * cells_];
        for (size_t k = 0; k < cells_; ++k) dA_[k] += (int32_t)s[k];
    }

    int64_t estimate(const std::vector<uint32_t> &col, const std::vector<int> &sg) const {
        return S::estimate(A_.data(), d_, w_, col, sg);
    }
    int64_t estimate_delta(const std::vector<uint32_t> &col, const std::vector<int> &sg) const {
        return S::estimate_delta(dA_.data(), d_, w_, col, sg);
    }

    // Contadores de la ventana: m sub-sketches + A (sin contar dA).
    size_t window_bytes() const { return (ring_.size() + A_.size()) * sizeof(Cell); }
    size_t delta_bytes() const { return dA_.size() * sizeof(int32_t); }

private:
    int m_, d_, w_;
    size_t cells_;
    std::vector<Cell> ring_, A_;
    std::vector<int32_t> dA_;
};

// Un ancho w con sus dos sketches.
struct Config {
    int w;
    SlidingWindow<CountMin> cms;
    SlidingWindow<CountSketch> cs;
    Config(int m, int d, int w_) : w(w_), cms(m, d, w_), cs(m, d, w_) {}
};

// -------------------------------------------------------------- utilidades

static bool parse_ipv4(const std::string &s, uint32_t *out) {
    unsigned a, b, c, d;
    char extra;
    if (sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    *out = (a << 24) | (b << 16) | (c << 8) | d;
    return true;
}

static std::string ip_str(uint32_t v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%u.%u.%u.%u", v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255);
    return buf;
}

static std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, sep)) out.push_back(tok);
    return out;
}

// Lee N por ventana desde un CSV de exact_hh (--out-windows o --out-query):
// basta que tenga columnas `win` y `N`.
static std::map<uint64_t, uint64_t> read_exact_N(const char *path) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "error: no se pudo abrir %s\n", path); exit(1); }
    std::string line;
    std::getline(f, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto hdr = split(line, ',');
    int iw = -1, in = -1;
    for (size_t i = 0; i < hdr.size(); ++i) {
        if (hdr[i] == "win") iw = (int)i;
        if (hdr[i] == "N") in = (int)i;
    }
    if (iw < 0 || in < 0) {
        fprintf(stderr, "error: %s no tiene columnas 'win' y 'N'\n", path);
        exit(1);
    }
    std::map<uint64_t, uint64_t> N;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto c = split(line, ',');
        if ((int)c.size() <= std::max(iw, in)) continue;
        N.emplace(strtoull(c[iw].c_str(), nullptr, 10), strtoull(c[in].c_str(), nullptr, 10));
    }
    return N;
}

static void usage(const char *p) {
    fprintf(stderr,
        "uso: %s TRAZA.bin [opciones]\n"
        "  --key K            src | dst (def. dst)\n"
        "  --query IP         IP a estimar; puede repetirse\n"
        "  --query-file ARCH  archivo con una IP por línea (se ignoran columnas extra)\n"
        "  -d D               filas de cada sketch (def. 5)\n"
        "  -w W[,W2,...]      ancho(s) de cada sketch (def. 4096)\n"
        "  --seed S           semilla de las funciones hash (def. 42)\n"
        "  -W SEGUNDOS        ancho de la ventana (def. 60)\n"
        "  --delta SEGUNDOS   largo de la subventana p (def. 10); W/p debe ser entero\n"
        "  --phi F            umbral de heavy hitter (def. 0.01)\n"
        "  --out ARCH         CSV de salida (def. salida estándar)\n"
        "  --verify ARCH      CSV de exact_hh con columnas win,N: compara N_j ventana\n"
        "                     por ventana y termina con código 3 si alguna difiere\n", p);
}

// ------------------------------------------------------------------- main

int main(int argc, char **argv) {
    if (argc < 2 || argv[1][0] == '-') { usage(argv[0]); return 2; }
    const char *path = argv[1];
    bool key_src = false;
    int d = 5;
    std::vector<int> widths = {4096};
    uint64_t seed = 42;
    double W_s = 60.0, p_s = 10.0, phi = 0.01;
    std::string out_s, verify_s;
    std::vector<uint32_t> queries;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "falta valor para %s\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--key") {
            std::string k = next();
            if (k == "src") key_src = true;
            else if (k == "dst") key_src = false;
            else { fprintf(stderr, "clave desconocida: %s (use src o dst)\n", k.c_str()); return 2; }
        } else if (a == "--query") {
            std::string s = next();
            uint32_t ip;
            if (!parse_ipv4(s, &ip)) { fprintf(stderr, "IP inválida: %s\n", s.c_str()); return 2; }
            queries.push_back(ip);
        } else if (a == "--query-file") {
            std::string fn = next();
            std::ifstream f(fn);
            if (!f) { fprintf(stderr, "no se pudo abrir %s\n", fn.c_str()); return 2; }
            std::string line;
            while (std::getline(f, line)) {
                auto c = split(line, ',');
                uint32_t ip;
                if (!c.empty() && parse_ipv4(c[0], &ip)) queries.push_back(ip);
            }
        } else if (a == "-d") d = atoi(next().c_str());
        else if (a == "-w") {
            widths.clear();
            for (auto &t : split(next(), ',')) widths.push_back(atoi(t.c_str()));
        } else if (a == "--seed") seed = strtoull(next().c_str(), nullptr, 10);
        else if (a == "-W") W_s = atof(next().c_str());
        else if (a == "--delta") p_s = atof(next().c_str());
        else if (a == "--phi") phi = atof(next().c_str());
        else if (a == "--out") out_s = next();
        else if (a == "--verify") verify_s = next();
        else if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else { fprintf(stderr, "opción no reconocida: %s\n", a.c_str()); return 2; }
    }

    // Mismas conversiones a microsegundos que exact_hh.
    const uint64_t W = (uint64_t)(W_s * 1e6), p = (uint64_t)(p_s * 1e6);
    if (d < 1 || widths.empty() || p == 0 || W == 0 || W % p != 0) {
        fprintf(stderr, "parámetros inválidos: se requiere d >= 1, p > 0 y W múltiplo de p\n");
        return 2;
    }
    for (int w : widths)
        if (w < 1) { fprintf(stderr, "ancho inválido: %d\n", w); return 2; }
    if (phi <= 0.0 || phi > 1.0) { fprintf(stderr, "--phi debe estar en (0,1]\n"); return 2; }
    if (queries.empty()) { fprintf(stderr, "se requiere al menos un --query o --query-file\n"); return 2; }
    const int m = (int)(W / p);
    const char *out_path = out_s.empty() ? nullptr : out_s.c_str();
    const char *verify_path = verify_s.empty() ? nullptr : verify_s.c_str();

    // ---------------------------------------------------- abrir la traza
    std::ifstream in(path, std::ios::binary);
    if (!in) { fprintf(stderr, "error: no se pudo abrir %s\n", path); return 1; }
    in.seekg(0, std::ios::end);
    const uint64_t bytes = (uint64_t)in.tellg();
    if (bytes == 0 || bytes % sizeof(Record)) {
        fprintf(stderr, "error: el tamaño de %s no es múltiplo de 24 B\n", path);
        return 1;
    }
    const uint64_t n_rec = bytes / sizeof(Record);
    Record first, last;
    in.seekg(0);
    in.read((char *)&first, sizeof first);
    in.seekg((std::streamoff)(bytes - sizeof(Record)));
    in.read((char *)&last, sizeof last);
    in.seekg(0);
    const uint64_t t0 = first.ts_us, tend = last.ts_us;

    // ------------------------------------------------- estructuras
    Hashes H(d, seed);
    std::vector<std::unique_ptr<Config>> cfg;
    for (int w : widths) cfg.emplace_back(new Config(m, d, w));
    const size_t nw = cfg.size();

    // Posiciones y signos precalculados de cada consulta, por ancho.
    std::vector<std::vector<std::vector<uint32_t>>> qcol(nw);
    std::vector<std::vector<int>> qsg(queries.size(), std::vector<int>(d));
    for (size_t z = 0; z < queries.size(); ++z)
        for (int i = 0; i < d; ++i) qsg[z][i] = H.sign(i, queries[z]);
    for (size_t c = 0; c < nw; ++c) {
        qcol[c].assign(queries.size(), std::vector<uint32_t>(d));
        for (size_t z = 0; z < queries.size(); ++z)
            for (int i = 0; i < d; ++i) qcol[c][z][i] = (uint32_t)(H.pos(i, queries[z]) % cfg[c]->w);
    }

    std::vector<uint64_t> Nring(m, 0);  // paquetes por subventana
    uint64_t N = 0;                     // paquetes en la ventana actual

    std::map<uint64_t, uint64_t> exactN;
    if (verify_path) exactN = read_exact_N(verify_path);
    size_t mismatches = 0, verified = 0;

    FILE *fo = out_path ? fopen(out_path, "w") : stdout;
    if (!fo) { perror("fopen --out"); return 1; }
    fprintf(fo, "win,tau_us,t_rel_s,key,w,N,threshold,cms_f,cms_hh,cs_f,cs_hh,"
                "cms_med_delta,cs_delta\n");

    // Evalúa la ventana j = q - m al cerrar la subventana q (tau = t0 + q p).
    size_t n_eval = 0;
    auto evaluate = [&](uint64_t q) {
        const uint64_t j = q - m, tau = t0 + q * p;
        const int slot = (int)((q - 1) % m);
        const bool has_delta = j >= 1;
        for (auto &c : cfg)
            if (has_delta) { c->cms.close_delta(slot); c->cs.close_delta(slot); }

        uint64_t thr = (uint64_t)std::ceil(phi * (double)N);
        if (thr == 0) thr = 1;

        if (verify_path) {
            auto it = exactN.find(j);
            if (it == exactN.end() || it->second != N) {
                if (mismatches < 10)
                    fprintf(stderr, "AUTOVERIFICACIÓN: ventana %" PRIu64 " N_j = %" PRIu64
                            " pero exact_hh da %s\n", j, N,
                            it == exactN.end() ? "(sin ventana)" : std::to_string(it->second).c_str());
                mismatches++;
            }
            verified++;
        }

        for (size_t c = 0; c < nw; ++c) {
            for (size_t z = 0; z < queries.size(); ++z) {
                const auto &col = qcol[c][z];
                const auto &sg = qsg[z];
                int64_t f_cms = cfg[c]->cms.estimate(col, sg);
                int64_t f_cs = std::max<int64_t>(0, cfg[c]->cs.estimate(col, sg));
                fprintf(fo, "%" PRIu64 ",%" PRIu64 ",%.6f,%s,%d,%" PRIu64 ",%" PRIu64
                            ",%" PRId64 ",%d,%" PRId64 ",%d,",
                        j, tau, (double)(tau - t0) / 1e6, ip_str(queries[z]).c_str(),
                        cfg[c]->w, N, thr, f_cms, f_cms >= (int64_t)thr ? 1 : 0, f_cs,
                        f_cs >= (int64_t)thr ? 1 : 0);
                if (has_delta)
                    fprintf(fo, "%" PRId64 ",%" PRId64 "\n", cfg[c]->cms.estimate_delta(col, sg),
                            cfg[c]->cs.estimate_delta(col, sg));
                else
                    fprintf(fo, ",\n");
            }
        }
        n_eval++;
    };

    // Abre la subventana q en su ranura; si q > m expira la que estaba allí.
    auto open_sub = [&](uint64_t q) {
        const int slot = (int)((q - 1) % m);
        if (q > (uint64_t)m) {
            for (auto &c : cfg) { c->cms.expire(slot); c->cs.expire(slot); }
            N -= Nring[slot];
            Nring[slot] = 0;
        }
    };
    // Cierra la subventana q: si completa una ventana dentro de la traza, se evalúa.
    auto close_sub = [&](uint64_t q) {
        if (q >= (uint64_t)m && t0 + q * p <= tend) evaluate(q);
    };

    // --------------------------------------------------------- pasada
    auto t_start = std::chrono::steady_clock::now();
    const size_t BUF = 1 << 16;
    std::vector<Record> buf(BUF);
    std::vector<uint64_t> raw(d);
    std::vector<uint32_t> col(d);
    std::vector<int> sg(d);
    uint64_t qcur = 1, read_total = 0, skipped_t0 = 0;
    open_sub(qcur);

    while (read_total < n_rec) {
        size_t want = (size_t)std::min<uint64_t>(BUF, n_rec - read_total);
        in.read((char *)buf.data(), want * sizeof(Record));
        size_t got = (size_t)in.gcount() / sizeof(Record);
        if (got == 0) { fprintf(stderr, "error de lectura\n"); return 1; }
        read_total += got;

        for (size_t r = 0; r < got; ++r) {
            const Record &pkt = buf[r];
            if (pkt.ts_us < t0) {
                fprintf(stderr, "error: la traza no está ordenada por tiempo\n");
                return 1;
            }
            if (pkt.ts_us == t0) { skipped_t0++; continue; }
            // q = ceil((ts - t0) / p): un paquete en el borde t0 + q p es de S_q.
            const uint64_t q = (pkt.ts_us - t0 + p - 1) / p;
            if (q < qcur) {
                fprintf(stderr, "error: la traza no está ordenada por tiempo\n");
                return 1;
            }
            while (q > qcur) {  // también recorre subventanas vacías
                close_sub(qcur);
                open_sub(++qcur);
            }

            const uint32_t x = key_src ? pkt.src : pkt.dst;
            for (int i = 0; i < d; ++i) { raw[i] = H.pos(i, x); sg[i] = H.sign(i, x); }
            const int slot = (int)((q - 1) % m);
            for (auto &c : cfg) {
                for (int i = 0; i < d; ++i) col[i] = (uint32_t)(raw[i] % c->w);
                c->cms.add(slot, col.data(), sg.data());
                c->cs.add(slot, col.data(), sg.data());
            }
            Nring[slot]++;
            N++;
        }
    }
    close_sub(qcur);
    if (fo != stdout) fclose(fo);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();

    // -------------------------------------------------------- resumen
    fprintf(stderr, "== sliding_sketch ==\n");
    fprintf(stderr, "traza                : %s (%" PRIu64 " registros, %.3f s)\n", path, n_rec,
            (double)(tend - t0) / 1e6);
    fprintf(stderr, "clave                : %s, %zu consulta(s)\n", key_src ? "src" : "dst",
            queries.size());
    fprintf(stderr, "W = %.0f s, p = %.0f s, m = %d, d = %d, phi = %g, semilla hash = %" PRIu64 "\n",
            W_s, p_s, m, d, phi, seed);
    fprintf(stderr, "paquetes en t0       : %" PRIu64 " (fuera de toda ventana, igual que exact_hh)\n",
            skipped_t0);
    fprintf(stderr, "ventanas evaluadas   : %zu\n", n_eval);
    fprintf(stderr, "tiempo de la pasada  : %.1f s\n", secs);
    for (auto &c : cfg) {
        fprintf(stderr, "w = %-6d CMS: %.2f MB (anillo + A = %d x %d x %d x 4 B) + %.2f MB dA\n",
                c->w, c->cms.window_bytes() / 1e6, m + 1, d, c->w, c->cms.delta_bytes() / 1e6);
        fprintf(stderr, "         CS : %.2f MB (anillo + A) + %.2f MB dA\n",
                c->cs.window_bytes() / 1e6, c->cs.delta_bytes() / 1e6);
    }
    fprintf(stderr, "anillo escalar N_j   : %d x 8 B\n", m);

    if (verify_path) {
        if (verified != exactN.size()) {
            fprintf(stderr, "AUTOVERIFICACIÓN: %zu ventanas evaluadas contra %zu en %s\n", verified,
                    exactN.size(), verify_path);
            mismatches++;
        }
        if (mismatches) {
            fprintf(stderr, "AUTOVERIFICACIÓN FALLIDA: %zu discrepancia(s). Resultados inválidos.\n",
                    mismatches);
            return 3;
        }
        fprintf(stderr, "autoverificación OK  : N_j coincide con exact_hh en las %zu ventanas\n",
                verified);
    }
    return 0;
}
