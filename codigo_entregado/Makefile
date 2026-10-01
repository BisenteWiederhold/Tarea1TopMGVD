CXX ?= g++
CXXFLAGS ?= -O2 -march=native -std=c++17 -Wall -Wextra

.PHONY: all clean
all: pcap2bin exact_hh sliding_sketch

pcap2bin: pcap2bin.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

exact_hh: exact_hh.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# En MSYS2/MinGW agregar -static (o LDFLAGS=-static) para no depender de las DLL de libstdc++.
sliding_sketch: sliding_sketch.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f pcap2bin exact_hh sliding_sketch sliding_sketch.exe
