// =============================================================================
//  bitreader.hpp — читання бітового потоку у форматі рушія Source (bf_read).
//
//  Мережеві повідомлення Source пакуються побітно: біти йдуть від молодшого
//  до старшого всередині кожного байта (little-endian). Наприклад, 6-бітний
//  тип повідомлення може починатися посеред байта.
//
//  При спробі читати за межами даних встановлюється прапорець overflowed(),
//  а функції повертають 0 — так парсер може перевірити помилку один раз.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gmdr::demo {

class BitReader {
public:
    BitReader() = default;
    BitReader(const uint8_t* data, size_t size_bytes)
        : data_(data), end_bits_(size_bytes * 8) {}
    BitReader(const uint8_t* data, size_t size_bytes, size_t size_bits)
        : data_(data), end_bits_(size_bits < size_bytes * 8 ? size_bits : size_bytes * 8) {}

    size_t position() const { return pos_; }
    size_t size_bits() const { return end_bits_; }
    size_t bits_left() const { return end_bits_ > pos_ ? end_bits_ - pos_ : 0; }
    bool   overflowed() const { return overflow_; }

    // Прочитати n (0..32) біт як беззнакове число.
    uint32_t read_ubits(int n) {
        if (n <= 0) return 0;
        if (n > 32 || pos_ + static_cast<size_t>(n) > end_bits_) {
            overflow_ = true;
            pos_ = end_bits_;
            return 0;
        }
        uint32_t result = 0;
        int got = 0;
        size_t pos = pos_;
        while (got < n) {
            const size_t byte = pos >> 3;
            const int bit = static_cast<int>(pos & 7);
            const int take = (8 - bit) < (n - got) ? (8 - bit) : (n - got);
            const uint32_t v = (static_cast<uint32_t>(data_[byte]) >> bit) & ((1u << take) - 1u);
            result |= v << got;
            got += take;
            pos += static_cast<size_t>(take);
        }
        pos_ = pos;
        return result;
    }

    int32_t read_sbits(int n) {
        const uint32_t v = read_ubits(n);
        if (n <= 0 || n >= 32) return static_cast<int32_t>(v);
        const uint32_t sign = 1u << (n - 1);
        return static_cast<int32_t>((v ^ sign) - sign);
    }

    bool     read_bit() { return read_ubits(1) != 0; }
    uint8_t  read_byte() { return static_cast<uint8_t>(read_ubits(8)); }
    uint16_t read_word() { return static_cast<uint16_t>(read_ubits(16)); }
    int16_t  read_short() { return static_cast<int16_t>(read_ubits(16)); }
    int32_t  read_long() { return static_cast<int32_t>(read_ubits(32)); }
    float    read_float() {
        const uint32_t u = read_ubits(32);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }

    // Protobuf-подібне число змінної довжини (використовує Source 2013).
    uint32_t read_varint32() {
        uint32_t result = 0;
        for (int i = 0; i < 5; ++i) {
            const uint32_t b = read_ubits(8);
            result |= (b & 0x7F) << (7 * i);
            if (!(b & 0x80)) return result;
        }
        overflow_ = true;   // занадто довге число — дані пошкоджені
        return result;
    }

    // Рядок, що закінчується нульовим байтом. Нуль споживається.
    std::string read_string(size_t max_len = 65536) {
        std::string s;
        while (!overflow_) {
            const char c = static_cast<char>(read_ubits(8));
            if (c == '\0') break;
            if (s.size() < max_len) s.push_back(c);
        }
        return s;
    }

    bool skip_bits(size_t n) {
        if (pos_ + n > end_bits_) {
            overflow_ = true;
            pos_ = end_bits_;
            return false;
        }
        pos_ += n;
        return true;
    }

    // Скопіювати n біт у масив байтів (вирівняний до байта результат).
    std::vector<uint8_t> read_bits_to_bytes(size_t nbits) {
        // Спершу перевірка меж, потім виділення: у битому файлі довжина може бути будь-якою
        if (pos_ + nbits > end_bits_) {
            overflow_ = true;
            pos_ = end_bits_;
            return {};
        }
        std::vector<uint8_t> out((nbits + 7) / 8, 0);
        if ((pos_ & 7) == 0) {
            // швидкий шлях: вирівняні дані
            if (nbits >= 8) std::memcpy(out.data(), data_ + (pos_ >> 3), nbits / 8);
            pos_ += (nbits / 8) * 8;
            if (nbits % 8) out[nbits / 8] = static_cast<uint8_t>(read_ubits(static_cast<int>(nbits % 8)));
            return out;
        }
        size_t i = 0;
        size_t left = nbits;
        while (left >= 8) {
            out[i++] = static_cast<uint8_t>(read_ubits(8));
            left -= 8;
        }
        if (left) out[i] = static_cast<uint8_t>(read_ubits(static_cast<int>(left)));
        return out;
    }

    // Координата у форматі Source (COORD_INTEGER_BITS=14, FRACTIONAL=5).
    float read_bit_coord() {
        const bool has_int = read_bit();
        const bool has_frac = read_bit();
        if (!has_int && !has_frac) return 0.0f;
        const bool negative = read_bit();
        int int_val = 0, frac_val = 0;
        if (has_int) int_val = static_cast<int>(read_ubits(14)) + 1;
        if (has_frac) frac_val = static_cast<int>(read_ubits(5));
        float v = static_cast<float>(int_val) + static_cast<float>(frac_val) * (1.0f / 32.0f);
        return negative ? -v : v;
    }

    void read_bit_vec3_coord(float out[3]) {
        const bool x = read_bit(), y = read_bit(), z = read_bit();
        out[0] = x ? read_bit_coord() : 0.0f;
        out[1] = y ? read_bit_coord() : 0.0f;
        out[2] = z ? read_bit_coord() : 0.0f;
    }

    // Чи всі біти, що залишилися, нульові (паддінг у кінці пакета).
    bool rest_is_zero() const {
        for (size_t p = pos_; p < end_bits_; ++p)
            if ((data_[p >> 3] >> (p & 7)) & 1) return false;
        return true;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t         pos_ = 0;
    size_t         end_bits_ = 0;
    bool           overflow_ = false;
};

} // namespace gmdr::demo
