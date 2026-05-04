#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <vector>

#include "qpsk_b200/symbol_mapper.h"

using namespace qpsk_b200;

static constexpr float SQRT1_2 = static_cast<float>(M_SQRT1_2);
static constexpr float TOL     = 1e-6f;

// Helper: compare two complex floats within tolerance
static void ExpectNear(std::complex<float> a, std::complex<float> b, float tol = TOL) {
    EXPECT_NEAR(a.real(), b.real(), tol);
    EXPECT_NEAR(a.imag(), b.imag(), tol);
}

// ---------------------------------------------------------------------------
// dibit_to_symbol — verify all four Gray-coded constellation points
// ---------------------------------------------------------------------------
TEST(SymbolMapper, DibitToSymbol_00) {
    auto sym = SymbolMapper::dibit_to_symbol(0);
    ExpectNear(sym, {+SQRT1_2, +SQRT1_2});
}

TEST(SymbolMapper, DibitToSymbol_01) {
    auto sym = SymbolMapper::dibit_to_symbol(1);
    ExpectNear(sym, {-SQRT1_2, +SQRT1_2});
}

TEST(SymbolMapper, DibitToSymbol_11) {
    auto sym = SymbolMapper::dibit_to_symbol(3);  // dibit 11 = numeric 3
    ExpectNear(sym, {-SQRT1_2, -SQRT1_2});
}

TEST(SymbolMapper, DibitToSymbol_10) {
    auto sym = SymbolMapper::dibit_to_symbol(2);  // dibit 10 = numeric 2
    ExpectNear(sym, {+SQRT1_2, -SQRT1_2});
}

TEST(SymbolMapper, DibitToSymbol_OutOfRange) {
    EXPECT_THROW(SymbolMapper::dibit_to_symbol(4), std::out_of_range);
    EXPECT_THROW(SymbolMapper::dibit_to_symbol(255), std::out_of_range);
}

// ---------------------------------------------------------------------------
// symbol_to_dibit — verify quadrant-based demapping
// ---------------------------------------------------------------------------
TEST(SymbolMapper, SymbolToDibit_AllQuadrants) {
    EXPECT_EQ(SymbolMapper::symbol_to_dibit({+1.0f, +1.0f}), 0);  // 00
    EXPECT_EQ(SymbolMapper::symbol_to_dibit({-1.0f, +1.0f}), 1);  // 01
    EXPECT_EQ(SymbolMapper::symbol_to_dibit({-1.0f, -1.0f}), 3);  // 11
    EXPECT_EQ(SymbolMapper::symbol_to_dibit({+1.0f, -1.0f}), 2);  // 10
}

TEST(SymbolMapper, SymbolToDibit_ExactConstellationPoints) {
    for (uint8_t d = 0; d < 4; ++d) {
        EXPECT_EQ(SymbolMapper::symbol_to_dibit(CONSTELLATION[d]), d);
    }
}

// ---------------------------------------------------------------------------
// map — even-length input (no padding)
// ---------------------------------------------------------------------------
TEST(SymbolMapper, Map_EvenLength) {
    // bits: 0,0 → dibit 00 (value 0) → (+SQRT1_2, +SQRT1_2)
    //       1,0 → dibit 10 (value 2) → (+SQRT1_2, -SQRT1_2)
    std::vector<uint8_t> bits = {0, 0, 1, 0};
    auto [symbols, padding] = SymbolMapper::map(bits);

    EXPECT_EQ(padding, 0);
    ASSERT_EQ(symbols.size(), 2u);
    ExpectNear(symbols[0], {+SQRT1_2, +SQRT1_2});  // 00
    ExpectNear(symbols[1], {+SQRT1_2, -SQRT1_2});   // 10 → value 2
}

// ---------------------------------------------------------------------------
// map — odd-length input (padding with one zero bit)
// ---------------------------------------------------------------------------
TEST(SymbolMapper, Map_OddLength) {
    // bits: {1} → padded to {1, 0} → dibit 10 (value 2) → (+SQRT1_2, -SQRT1_2)
    std::vector<uint8_t> bits = {1};
    auto [symbols, padding] = SymbolMapper::map(bits);

    EXPECT_EQ(padding, 1);
    ASSERT_EQ(symbols.size(), 1u);
    ExpectNear(symbols[0], {+SQRT1_2, -SQRT1_2});  // dibit 10 → value 2
}

// ---------------------------------------------------------------------------
// map — empty input
// ---------------------------------------------------------------------------
TEST(SymbolMapper, Map_Empty) {
    std::vector<uint8_t> bits;
    auto [symbols, padding] = SymbolMapper::map(bits);

    EXPECT_EQ(padding, 0);
    EXPECT_TRUE(symbols.empty());
}

// ---------------------------------------------------------------------------
// demap — round-trip through all four constellation points
// ---------------------------------------------------------------------------
TEST(SymbolMapper, Demap_AllConstellationPoints) {
    std::vector<std::complex<float>> symbols = {
        CONSTELLATION[0],  // 00
        CONSTELLATION[1],  // 01
        CONSTELLATION[3],  // 11
        CONSTELLATION[2],  // 10
    };
    auto bits = SymbolMapper::demap(symbols);

    ASSERT_EQ(bits.size(), 8u);
    // 00
    EXPECT_EQ(bits[0], 0); EXPECT_EQ(bits[1], 0);
    // 01
    EXPECT_EQ(bits[2], 0); EXPECT_EQ(bits[3], 1);
    // 11
    EXPECT_EQ(bits[4], 1); EXPECT_EQ(bits[5], 1);
    // 10
    EXPECT_EQ(bits[6], 1); EXPECT_EQ(bits[7], 0);
}

// ---------------------------------------------------------------------------
// demap — empty input
// ---------------------------------------------------------------------------
TEST(SymbolMapper, Demap_Empty) {
    std::vector<std::complex<float>> symbols;
    auto bits = SymbolMapper::demap(symbols);
    EXPECT_TRUE(bits.empty());
}

// ---------------------------------------------------------------------------
// map → demap round trip (even length)
// ---------------------------------------------------------------------------
TEST(SymbolMapper, MapDemap_RoundTrip_Even) {
    std::vector<uint8_t> original = {1, 0, 0, 1, 1, 1, 0, 0};
    auto [symbols, padding] = SymbolMapper::map(original);
    EXPECT_EQ(padding, 0);

    auto recovered = SymbolMapper::demap(symbols);
    EXPECT_EQ(recovered, original);
}

// ---------------------------------------------------------------------------
// map → demap round trip (odd length — padding bit must be stripped by caller)
// ---------------------------------------------------------------------------
TEST(SymbolMapper, MapDemap_RoundTrip_Odd) {
    std::vector<uint8_t> original = {1, 0, 1};
    auto [symbols, padding] = SymbolMapper::map(original);
    EXPECT_EQ(padding, 1);

    auto recovered = SymbolMapper::demap(symbols);
    // recovered has the padding bit appended: {1, 0, 1, 0}
    ASSERT_EQ(recovered.size(), 4u);
    // First 3 bits match original
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(recovered[i], original[i]);
    }
    // Padding bit is 0
    EXPECT_EQ(recovered[3], 0);
}

// ---------------------------------------------------------------------------
// dibit_to_symbol → symbol_to_dibit round trip for all dibits
// ---------------------------------------------------------------------------
TEST(SymbolMapper, DibitRoundTrip_All) {
    for (uint8_t d = 0; d < 4; ++d) {
        auto sym = SymbolMapper::dibit_to_symbol(d);
        EXPECT_EQ(SymbolMapper::symbol_to_dibit(sym), d)
            << "Round-trip failed for dibit " << static_cast<int>(d);
    }
}
