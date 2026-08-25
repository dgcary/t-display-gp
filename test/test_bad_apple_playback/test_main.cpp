#include <unity.h>

#include <cstdint>

#include "BadApplePlayback.h"

void setUp() {}
void tearDown() {}

void test_timeline_advances_at_ten_fps_and_loops() {
  TEST_ASSERT_EQUAL_UINT32(0, BadApplePlayback::frameIndex(0));
  TEST_ASSERT_EQUAL_UINT32(0, BadApplePlayback::frameIndex(99));
  TEST_ASSERT_EQUAL_UINT32(1, BadApplePlayback::frameIndex(100));
  TEST_ASSERT_EQUAL_UINT32(2189, BadApplePlayback::frameIndex(218999));
  TEST_ASSERT_EQUAL_UINT32(0, BadApplePlayback::frameIndex(219000));
  TEST_ASSERT_EQUAL_UINT32(1, BadApplePlayback::frameIndex(219100));
}

void test_layout_constants_match_approved_viewport() {
  TEST_ASSERT_EQUAL_UINT16(168, BadApplePlayback::WIDTH);
  TEST_ASSERT_EQUAL_UINT16(126, BadApplePlayback::HEIGHT);
  TEST_ASSERT_EQUAL_UINT16(21, BadApplePlayback::ROW_BYTES);
  TEST_ASSERT_EQUAL_UINT32(2646, BadApplePlayback::FRAME_BYTES);
  TEST_ASSERT_EQUAL_UINT32(2190, BadApplePlayback::FRAME_COUNT);
}

void test_delta_decoder_xors_sparse_literal_runs() {
  uint8_t frame[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  const uint8_t delta[] = {
      2, 2, 0xFF, 0x0F,  // skip 2, xor two bytes
      1, 1, 0xAA,        // skip one, xor final byte
  };
  TEST_ASSERT_TRUE(BadApplePlayback::applyDelta(frame, sizeof(frame), delta, sizeof(delta)));
  TEST_ASSERT_EQUAL_HEX8(0x00, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(0x11, frame[1]);
  TEST_ASSERT_EQUAL_HEX8(0xDD, frame[2]);
  TEST_ASSERT_EQUAL_HEX8(0x3C, frame[3]);
  TEST_ASSERT_EQUAL_HEX8(0x44, frame[4]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, frame[5]);
}

void test_delta_decoder_rejects_truncated_or_out_of_bounds_stream() {
  uint8_t frame[] = {0, 0, 0, 0};
  const uint8_t truncated[] = {0, 2, 0xFF};
  TEST_ASSERT_FALSE(BadApplePlayback::applyDelta(frame, sizeof(frame), truncated, sizeof(truncated)));
  const uint8_t outOfBounds[] = {5, 1, 0x01};
  TEST_ASSERT_FALSE(BadApplePlayback::applyDelta(frame, sizeof(frame), outOfBounds, sizeof(outOfBounds)));
  const uint8_t badVarint[] = {0x80};
  TEST_ASSERT_FALSE(BadApplePlayback::applyDelta(frame, sizeof(frame), badVarint, sizeof(badVarint)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_timeline_advances_at_ten_fps_and_loops);
  RUN_TEST(test_layout_constants_match_approved_viewport);
  RUN_TEST(test_delta_decoder_xors_sparse_literal_runs);
  RUN_TEST(test_delta_decoder_rejects_truncated_or_out_of_bounds_stream);
  return UNITY_END();
}
