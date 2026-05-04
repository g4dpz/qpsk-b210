#ifndef QPSK_B200_SYMBOL_MAPPER_H
#define QPSK_B200_SYMBOL_MAPPER_H

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <utility>
#include <vector>

namespace qpsk_b200 {

/// Gray-coded QPSK constellation.
/// Index is the numeric dibit value (0–3); value is the corresponding I/Q point.
///   00 (0) → π/4,  01 (1) → 3π/4,  10 (2) → 7π/4,  11 (3) → 5π/4
inline const std::array<std::complex<float>, 4> CONSTELLATION = {{
    { static_cast<float>(M_SQRT1_2),  static_cast<float>(M_SQRT1_2)},   // 00 → π/4
    {-static_cast<float>(M_SQRT1_2),  static_cast<float>(M_SQRT1_2)},   // 01 → 3π/4
    { static_cast<float>(M_SQRT1_2), -static_cast<float>(M_SQRT1_2)},   // 10 → 7π/4
    {-static_cast<float>(M_SQRT1_2), -static_cast<float>(M_SQRT1_2)},   // 11 → 5π/4
}};

/// QPSK symbol mapper / demapper using Gray coding.
class SymbolMapper {
public:
    /// Map a bit vector to QPSK constellation symbols.
    ///
    /// Each element of @p bits must be 0 or 1.
    /// Bits are grouped into dibits (MSB first).  If the input length is odd,
    /// one zero-bit is appended as padding.
    ///
    /// @return {symbols, padding_bits} where padding_bits is 0 or 1.
    static std::pair<std::vector<std::complex<float>>, uint8_t>
    map(const std::vector<uint8_t>& bits);

    /// Demap QPSK symbols back to a bit vector.
    ///
    /// Each symbol is assigned to the nearest constellation quadrant and the
    /// corresponding two bits are appended to the output.
    ///
    /// @return Bit vector (each element is 0 or 1), length = 2 × symbols.size().
    static std::vector<uint8_t>
    demap(const std::vector<std::complex<float>>& symbols);

    /// Convert a single dibit (0–3) to its constellation point.
    static std::complex<float> dibit_to_symbol(uint8_t dibit);

    /// Convert a single constellation point to its dibit (0–3) by quadrant.
    static uint8_t symbol_to_dibit(std::complex<float> sym);
};

} // namespace qpsk_b200

#endif // QPSK_B200_SYMBOL_MAPPER_H
