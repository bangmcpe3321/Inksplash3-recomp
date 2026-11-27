#include "../../runtime/hid_gyro_cal.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

static void put16(std::uint8_t* p, std::int16_t v) {
    const auto u = static_cast<std::uint16_t>(v);
    p[0] = static_cast<std::uint8_t>(u);
    p[1] = static_cast<std::uint8_t>(u >> 8);
}

static std::array<std::uint8_t, 64> reply(std::uint32_t address, std::uint8_t length) {
    std::array<std::uint8_t, 64> r{};
    r[0] = 0x21;
    r[13] = 0x90;
    r[14] = 0x10;
    for (int i = 0; i < 4; ++i) r[15 + i] = static_cast<std::uint8_t>(address >> (8 * i));
    r[19] = length;
    return r;
}

int main() {
    using namespace hid_gyro;
    constexpr float two_pi = 6.28318530718f;
    assert(drift_threshold_rev_s(0) * two_pi > 0.0099f && drift_threshold_rev_s(0) * two_pi < 0.0101f);
    assert(drift_threshold_rev_s(1) * two_pi > 0.0069f && drift_threshold_rev_s(1) * two_pi < 0.0071f);
    assert(drift_threshold_rev_s(2) * two_pi > 0.0019f && drift_threshold_rev_s(2) * two_pi < 0.0021f);
    assert(bias_learning_rest_threshold_rev_s() * two_pi > 0.0499f &&
           bias_learning_rest_threshold_rev_s() * two_pi < 0.0501f);

    ImuCalibration cal;

    auto factory = reply(0x6020, 24);
    for (int i = 0; i < 3; ++i) {
        put16(&factory[20 + 2 * i], static_cast<std::int16_t>(10 + i));
        put16(&factory[26 + 2 * i], static_cast<std::int16_t>(16400 + i));
        put16(&factory[32 + 2 * i], static_cast<std::int16_t>(100 + i));
        put16(&factory[38 + 2 * i], static_cast<std::int16_t>(13400 + i));
    }
    assert(parse_spi_reply(factory.data(), factory.size(), &cal));
    assert(cal.factory && !cal.user_offsets);
    assert(cal.accel_offset[2] == 12 && cal.gyro_offset[1] == 101);

    const std::int16_t raw_a[3] = {410, 820, -1230};
    const std::int16_t raw_g[3] = {110, 101, 103};
    float accel[3], gyro[3];
    convert_raw(raw_a, raw_g, cal, accel, gyro);
    assert(accel[0] > 0.10f && accel[0] < 0.11f);
    assert(gyro[0] > 0.68f && gyro[0] < 0.71f);
    assert(gyro[2] > 0.06f && gyro[2] < 0.08f);

    auto user = reply(0x8026, 20);
    user[20] = 0xB2;
    user[21] = 0xA1;
    for (int i = 0; i < 3; ++i) {
        put16(&user[22 + 2 * i], static_cast<std::int16_t>(20 + i));
        put16(&user[34 + 2 * i], static_cast<std::int16_t>(200 + i));
    }
    assert(parse_spi_reply(user.data(), user.size(), &cal));
    assert(cal.factory && cal.user_offsets && cal.accel_offset[0] == 20 && cal.gyro_offset[2] == 202);
    assert(cal.accel_scale[0] == 16400 && cal.gyro_scale[0] == 13400);

    const auto preserved = cal;
    auto bad = reply(0x6020, 24);
    bad[13] = 0;
    assert(!parse_spi_reply(bad.data(), bad.size(), &cal));
    assert(cal.accel_offset[0] == preserved.accel_offset[0] && cal.gyro_scale[0] == preserved.gyro_scale[0]);
    assert(!parse_spi_reply(factory.data(), 40, &cal));
    auto wrong_magic = reply(0x8026, 20);
    assert(!parse_spi_reply(wrong_magic.data(), wrong_magic.size(), &cal));

    std::puts("Nintendo gyro calibration tests passed");
}
