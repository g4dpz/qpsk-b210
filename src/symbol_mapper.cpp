#include "qpsk_b200/symbol_mapper.h"

#include <stdexcept>

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// dibit_to_symbol  —  look up the constellation point for a dibit (0–3)
// ---------------------------------------------------------------------------
std::complex<float> SymbolMapper::dibit_to_symbol(uint8_t dibit) {
    if (dibit > 3) {
        throw std::out_of_range("dibit_to_symbol: dibit must be 0–3, got " +
                                std::to_string(dibit));
    }
    return CONSTELLATION[dibit];
}

// ---------------------------------------------------------------------------
// symbol_to_dibit  —  hard-decision demapping by quadrant
//
//   I > 0, Q ≥ 0  →  00  (index 0)
//   I ≤ 0, Q > 0  →  01  (index 1)
//   I ≤ 0, Q ≤ 0  →  11  (index 2)
//   I > 0, Q < 0  →  10  (index 3)
// ---------------------------------------------------------------------------
uint8_t SymbolMapper::symbol_to_dibit(std::complex<float> sym) {
    const float I = sym.real();
    const float Q = sym.imag();

    if (I > 0.0f && Q >= 0.0f) return 0;  // 00
    if (I <= 0.0f && Q > 0.0f) return 1;  // 01
    if (I <= 0.0f && Q <= 0.0f) return 3;  // 11
    /* I > 0, Q < 0 */        return 2;  // 10
}

// ---------------------------------------------------------------------------
// map  —  bit vector → QPSK symbols
// ---------------------------------------------------------------------------
std::pair<std::vector<std::complex<float>>, uint8_t>
SymbolMapper::map(const std::vector<uint8_t>& bits) {
    uint8_t padding = 0;
    std::vector<uint8_t> padded_bits = bits;

    // Pad with one zero bit if the length is odd
    if (padded_bits.size() % 2 != 0) {
        padded_bits.push_back(0);
        padding = 1;
    }

    const size_t num_symbols = padded_bits.size() / 2;
    std::vector<std::complex<float>> symbols;
    symbols.reserve(num_symbols);

    for (size_t i = 0; i < padded_bits.size(); i += 2) {
        // Form dibit: first bit is MSB, second bit is LSB
        uint8_t dibit = static_cast<uint8_t>((padded_bits[i] << 1) | padded_bits[i + 1]);
        symbols.push_back(CONSTELLATION[dibit]);
    }

    return {symbols, padding};
}

// ---------------------------------------------------------------------------
// demap  —  QPSK symbols → bit vector
// ---------------------------------------------------------------------------
std::vector<uint8_t>
SymbolMapper::demap(const std::vector<std::complex<float>>& symbols) {
    std::vector<uint8_t> bits;
    bits.reserve(symbols.size() * 2);

    for (const auto& sym : symbols) {
        uint8_t dibit = symbol_to_dibit(sym);
        bits.push_back((dibit >> 1) & 1);  // MSB
        bits.push_back(dibit & 1);          // LSB
    }

    return bits;
}

} // namespace qpsk_b200
