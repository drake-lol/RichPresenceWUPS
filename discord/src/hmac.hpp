#pragma once

#include <cstdint>
#include <cstring>
#include <string>

/**
 * A small SHA-256 and HMAC-SHA256 implementation, used to sign updates sent to a remote server.
 * Keep this file identical to the copy in the computer application.
 */
namespace Hmac {
    const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    inline uint32_t Rotr(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }

    inline void Transform(uint32_t state[8], const uint8_t block[64]) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t) block[i * 4] << 24 | (uint32_t) block[i * 4 + 1] << 16 | (uint32_t) block[i * 4 + 2] << 8 | block[i * 4 + 3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    /**
     * Hashes data with SHA-256.
     * @return The 32 byte digest.
     */
    inline std::string Sha256(const std::string &data) {
        uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

        // Pad with 0x80, zeros, then the length in bits, to a multiple of 64 bytes
        std::string msg = data;
        uint64_t bits = (uint64_t) data.size() * 8;
        msg += (char) 0x80;
        while (msg.size() % 64 != 56) msg += (char) 0;
        for (int i = 7; i >= 0; i--) msg += (char) (bits >> (i * 8));

        for (size_t i = 0; i < msg.size(); i += 64) {
            Transform(state, reinterpret_cast<const uint8_t *>(msg.data() + i));
        }

        std::string digest(32, '\0');
        for (int i = 0; i < 8; i++) {
            digest[i * 4]     = (char) (state[i] >> 24);
            digest[i * 4 + 1] = (char) (state[i] >> 16);
            digest[i * 4 + 2] = (char) (state[i] >> 8);
            digest[i * 4 + 3] = (char) state[i];
        }
        return digest;
    }

    /**
     * Computes an HMAC-SHA256 of a message.
     * @return The signature as lowercase hex.
     */
    inline std::string Sign(std::string key, const std::string &message) {
        if (key.size() > 64) key = Sha256(key);
        key.resize(64, '\0');

        std::string inner(64, '\0'), outer(64, '\0');
        for (int i = 0; i < 64; i++) {
            inner[i] = key[i] ^ 0x36;
            outer[i] = key[i] ^ 0x5c;
        }
        std::string mac = Sha256(outer + Sha256(inner + message));

        const char *hex = "0123456789abcdef";
        std::string out;
        for (unsigned char c : mac) {
            out += hex[c >> 4];
            out += hex[c & 0xf];
        }
        return out;
    }

    /**
     * Builds the message that is signed: the timestamp and the body, separated by a newline.
     */
    inline std::string Message(const std::string &timestamp, const std::string &body) {
        return timestamp + "\n" + body;
    }
}
