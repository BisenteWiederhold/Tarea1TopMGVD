#include <iostream>
#include <fstream>
#include <cstdint>
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>

#pragma pack(push, 1)
struct Record {
    uint64_t ts_us;
    uint32_t src, dst;
    uint16_t sport, dport, len;
    uint8_t proto, flags;
};
#pragma pack(pop)
static_assert(sizeof(Record) == 24, "el registro debe ocupar 24 bytes");

const uint64_t PRIME = 2147483647;

struct HashFamily {
    std::vector<uint64_t> a, b;
    std::vector<uint64_t> a_sign, b_sign; 
    
    HashFamily(int d, int seed = 42) {
        std::mt19937_64 rng(seed); 
        std::uniform_int_distribution<uint64_t> dist_a(1, PRIME - 1);
        std::uniform_int_distribution<uint64_t> dist_b(0, PRIME - 1);
        for (int i = 0; i < d; ++i) {
            a.push_back(dist_a(rng));
            b.push_back(dist_b(rng));
            a_sign.push_back(dist_a(rng));
            b_sign.push_back(dist_b(rng));
        }
    }
    
    inline int hash(uint32_t x, int fila, int w) const {
        return ((a[fila] * x + b[fila]) % PRIME) % w;
    }
    
    inline int sign(uint32_t x, int fila) const {
        int val = ((a_sign[fila] * x + b_sign[fila]) % PRIME) % 2;
        return val == 1 ? 1 : -1;
    }
};

int main() {
    const double W = 60.0;
    const double delta = 10.0;
    const int m = static_cast<int>(W / delta);
    const int d = 5;       
    const int w = 4096;    
    
    int32_t* CS = new int32_t[m * d * w](); 
    HashFamily hasher(d);

    long long N_j[6] = {0}; 
    double t_0 = -1.0;      
    long long q_actual = 1; 

    std::ifstream file("traza_ddos.bin", std::ios::binary);
    std::ofstream out_csv("resultados_cs.csv"); // Archivo de salida
    
    if (!file) return 1;

    Record pkt;
    while (file.read(reinterpret_cast<char*>(&pkt), sizeof(Record))) {
        
        double ts_segundos = pkt.ts_us / 1000000.0;
        if (t_0 < 0) t_0 = ts_segundos; 

        long long q_paquete = static_cast<long long>((ts_segundos - t_0) / delta) + 1;
        
        while (q_paquete > q_actual) {
            if (q_actual >= m) {
                long long suma_N = 0;
                for (int i = 0; i < m; ++i) suma_N += N_j[i];
                
                uint32_t ip_victima = (163 << 24) | (210 << 16) | (30 << 8) | 13;
                std::vector<long long> estimaciones(d);
                
                for (int i = 0; i < d; ++i) {
                    int columna = hasher.hash(ip_victima, i, w);
                    int signo = hasher.sign(ip_victima, i); 
                    long long suma_ventanas = 0;
                    
                    for (int r = 0; r < m; ++r) {
                        int index = (r * d * w) + (i * w) + columna;
                        suma_ventanas += CS[index]; 
                    }
                    estimaciones[i] = suma_ventanas * signo;
                }

                std::sort(estimaciones.begin(), estimaciones.end());
                long long f_estimado = estimaciones[d / 2]; 

                // Escribir directamente en el CSV
                out_csv << q_actual << "," << suma_N << "," << f_estimado << "\n";
            }
            
            q_actual++;
            int ranura_a_limpiar = (q_actual - 1) % m;
            N_j[ranura_a_limpiar] = 0;
            int offset_inicio = ranura_a_limpiar * (d * w);
            int offset_fin = offset_inicio + (d * w);
            std::fill(CS + offset_inicio, CS + offset_fin, 0); 
        }
        
        int ranura = (q_paquete - 1) % m;
        N_j[ranura]++; 
        for (int i = 0; i < d; ++i) {
            int columna = hasher.hash(pkt.dst, i, w);
            int signo = hasher.sign(pkt.dst, i); 
            int index = (ranura * d * w) + (i * w) + columna;
            CS[index] += signo; 
        }
    }

    file.close();
    out_csv.close(); // Cerrar el CSV
    delete[] CS; 
    return 0;
}