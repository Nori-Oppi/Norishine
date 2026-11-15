/**
 * @file tests/unit/test_utility.cpp
 * @brief Test src/utility.h hex decoding.
 */
#include "../tests_common.h"

#include <array>
#include <cstdint>

#include <src/utility.h>

TEST(UtilityFromHexTests, DecodesExactLengthInput) {
  EXPECT_EQ(util::from_hex<std::uint32_t>("12345678"), util::endian::little(std::uint32_t {0x12345678}));
  EXPECT_EQ(util::from_hex<std::uint32_t>("12345678", true), util::endian::big(std::uint32_t {0x12345678}));
}

TEST(UtilityFromHexTests, ShortInputPadsUnusedBytesWithZeros) {
  EXPECT_EQ(util::from_hex<std::uint64_t>("ABCD"), util::endian::little(std::uint64_t {0xABCD}));
  EXPECT_EQ(util::from_hex<std::uint64_t>("ABCD", true), util::endian::big(std::uint64_t {0xABCD}));
}

// Sunshine f86c4c9c: longer input used to overflow the stack buffer.
TEST(UtilityFromHexTests, LongInputKeepsTheLowOrderBytes) {
  EXPECT_EQ(util::from_hex<std::uint32_t>("123456789A"), util::endian::little(std::uint32_t {0x3456789A}));
  EXPECT_EQ(util::from_hex<std::uint32_t>("123456789A", true), util::endian::big(std::uint32_t {0x3456789A}));
  EXPECT_EQ(util::from_hex<std::uint8_t>(std::string(4096, 'f')), std::uint8_t {0xFF});
}

// Sunshine 127b4948: an odd digit count used to read before the start of the input.
TEST(UtilityFromHexTests, OddDigitCountReadsTheLeadingDigitAsTheHighNibble) {
  EXPECT_EQ(util::from_hex<std::uint32_t>("8"), util::endian::little(std::uint32_t {0x8}));
  EXPECT_EQ(util::from_hex<std::uint16_t>("ABC"), util::endian::little(std::uint16_t {0xABC}));
  EXPECT_EQ(util::from_hex<std::uint16_t>("ABC", true), util::endian::big(std::uint16_t {0xABC}));
  EXPECT_EQ(util::from_hex<std::uint16_t>("1:2:3"), util::endian::little(std::uint16_t {0x123}));
}

TEST(UtilityFromHexTests, SkipsSeparatorsAndAcceptsBothCases) {
  const auto bytes = util::from_hex<std::array<std::uint8_t, 4>>("12:34:aB:Cd", true);
  EXPECT_EQ(bytes, (std::array<std::uint8_t, 4> {0x12, 0x34, 0xAB, 0xCD}));
}

TEST(UtilityFromHexTests, EmptyInputIsZero) {
  EXPECT_EQ(util::from_hex<std::uint32_t>(""), std::uint32_t {0});
}

TEST(UtilityFromHexVecTests, DecodesBytesInOrder) {
  EXPECT_EQ(util::from_hex_vec("0a0B10", true), std::string("\x0a\x0b\x10", 3));
}
