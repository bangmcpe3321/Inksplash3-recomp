#pragma once

#include <cstddef>
#include <cstdint>

namespace hid_gyro {

inline float drift_threshold_rev_s(std::uint32_t mode) {
    const float rad_s = mode == 0 ? 0.01f : mode == 2 ? 0.002f : 0.007f;
    return rad_s / 6.28318530718f;
}

inline float bias_learning_rest_threshold_rev_s() {
    // Eden's automatic bias update uses IsAtRestRelaxed = 0.05 rad/s for every mode.
    return 0.05f / 6.28318530718f;
}

struct ImuCalibration {
    std::int16_t accel_offset[3] = {};
    std::int16_t accel_scale[3] = {16384, 16384, 16384};
    std::int16_t gyro_offset[3] = {};
    std::int16_t gyro_scale[3] = {13371, 13371, 13371};
    bool factory = false;
    bool user_offsets = false;
};

inline std::int16_t read_le16(const std::uint8_t* p) {
    return static_cast<std::int16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}

inline bool valid(const ImuCalibration& cal) {
    for (int i = 0; i < 3; ++i) {
        const int accel_range = static_cast<int>(cal.accel_scale[i]) - cal.accel_offset[i];
        const int gyro_range = static_cast<int>(cal.gyro_scale[i]) - cal.gyro_offset[i];
        if (accel_range < 1000 || accel_range > 32767 || gyro_range < 1000 || gyro_range > 32767) return false;
    }
    return true;
}

// Parses the payload layout of a Switch 0x21 SPI-read reply. Invalid or partial
// replies leave the current calibration untouched.
inline bool parse_spi_reply(const std::uint8_t* r, std::size_t n, ImuCalibration* cal) {
    if (!r || !cal || n < 20 || r[0] != 0x21 || !(r[13] & 0x80) || r[14] != 0x10) return false;
    const std::uint32_t address = static_cast<std::uint32_t>(r[15]) |
                                  (static_cast<std::uint32_t>(r[16]) << 8) |
                                  (static_cast<std::uint32_t>(r[17]) << 16) |
                                  (static_cast<std::uint32_t>(r[18]) << 24);
    const std::uint8_t length = r[19];
    if (n < 20u + length) return false;
    const std::uint8_t* d = r + 20;
    ImuCalibration candidate = *cal;
    if (address == 0x6020 && length >= 24) {
        for (int i = 0; i < 3; ++i) {
            candidate.accel_offset[i] = read_le16(d + 2 * i);
            candidate.accel_scale[i] = read_le16(d + 6 + 2 * i);
            candidate.gyro_offset[i] = read_le16(d + 12 + 2 * i);
            candidate.gyro_scale[i] = read_le16(d + 18 + 2 * i);
        }
        if (!valid(candidate)) return false;
        candidate.factory = true;
    } else if (address == 0x8026 && length >= 20 && d[0] == 0xB2 && d[1] == 0xA1) {
        for (int i = 0; i < 3; ++i) {
            candidate.accel_offset[i] = read_le16(d + 2 + 2 * i);
            candidate.gyro_offset[i] = read_le16(d + 14 + 2 * i);
        }
        if (!valid(candidate)) return false;
        candidate.user_offsets = true;
    } else {
        return false;
    }
    *cal = candidate;
    return true;
}

inline void convert_raw(const std::int16_t raw_accel[3], const std::int16_t raw_gyro[3], const ImuCalibration& cal,
                        float accel_g[3], float gyro_dps[3]) {
    for (int i = 0; i < 3; ++i) {
        const float accel_range = static_cast<float>(cal.accel_scale[i]) - cal.accel_offset[i];
        const float gyro_range = static_cast<float>(cal.gyro_scale[i]) - cal.gyro_offset[i];
        accel_g[i] = raw_accel[i] * (4.0f / accel_range);
        gyro_dps[i] = (raw_gyro[i] - cal.gyro_offset[i]) * (936.0f / gyro_range);
    }
}

} // namespace hid_gyro
