#pragma once

// Codificador/decodificador protobuf mínimo: só o necessário para as poucas
// mensagens de IAuthenticationService (varint, fixed64, length-delimited).

#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pb {

class Writer {
public:
    Writer& varint(uint32_t field, uint64_t v) { tag(field, 0); raw_varint(v); return *this; }
    Writer& int32(uint32_t field, int32_t v) { return varint(field, static_cast<uint64_t>(static_cast<int64_t>(v))); }
    Writer& fixed64(uint32_t field, uint64_t v) {
        tag(field, 1);
        for (int i = 0; i < 8; ++i) buf_ += static_cast<char>((v >> (8 * i)) & 0xFF);
        return *this;
    }
    Writer& bytes(uint32_t field, std::string_view v) {
        tag(field, 2);
        raw_varint(v.size());
        buf_.append(v);
        return *this;
    }
    Writer& message(uint32_t field, const Writer& sub) { return bytes(field, sub.buf_); }

    const std::string& data() const { return buf_; }

private:
    void tag(uint32_t field, uint32_t wire) { raw_varint((static_cast<uint64_t>(field) << 3) | wire); }
    void raw_varint(uint64_t v) {
        while (v >= 0x80) { buf_ += static_cast<char>((v & 0x7F) | 0x80); v >>= 7; }
        buf_ += static_cast<char>(v);
    }
    std::string buf_;
};

// Campos decodificados: número → valores (varint/fixed como inteiro, length-delimited como bytes).
struct Message {
    std::map<uint32_t, std::vector<uint64_t>> ints;
    std::map<uint32_t, std::vector<std::string>> blobs;

    uint64_t u64(uint32_t f, uint64_t def = 0) const {
        auto it = ints.find(f);
        return it == ints.end() || it->second.empty() ? def : it->second.front();
    }
    std::string str(uint32_t f) const {
        auto it = blobs.find(f);
        return it == blobs.end() || it->second.empty() ? std::string{} : it->second.front();
    }
    float f32(uint32_t f, float def = 0) const {
        auto it = ints.find(f);
        if (it == ints.end() || it->second.empty()) return def;
        uint32_t bits = static_cast<uint32_t>(it->second.front());
        float out;
        static_assert(sizeof out == sizeof bits);
        std::memcpy(&out, &bits, sizeof out);
        return out;
    }
};

inline Message parse(std::string_view in) {
    Message m;
    size_t i = 0;
    auto read_varint = [&]() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (i >= in.size()) throw std::runtime_error("protobuf truncado");
            auto b = static_cast<uint8_t>(in[i++]);
            v |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
        }
        throw std::runtime_error("varint inválido");
    };
    auto read_fixed = [&](int n) {
        if (i + n > in.size()) throw std::runtime_error("protobuf truncado");
        uint64_t v = 0;
        for (int k = 0; k < n; ++k) v |= static_cast<uint64_t>(static_cast<uint8_t>(in[i++])) << (8 * k);
        return v;
    };
    while (i < in.size()) {
        uint64_t key = read_varint();
        auto field = static_cast<uint32_t>(key >> 3);
        switch (key & 7) {
        case 0: m.ints[field].push_back(read_varint()); break;
        case 1: m.ints[field].push_back(read_fixed(8)); break;
        case 5: m.ints[field].push_back(read_fixed(4)); break;
        case 2: {
            uint64_t len = read_varint();
            if (i + len > in.size()) throw std::runtime_error("protobuf truncado");
            m.blobs[field].emplace_back(in.substr(i, len));
            i += len;
            break;
        }
        default: throw std::runtime_error("wire type não suportado");
        }
    }
    return m;
}

} // namespace pb
