#include "sha1.h"

#include <cstring>

namespace {

inline quint32 rotl(quint32 value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

}

QByteArray Sha1::hash(const QByteArray &input)
{
    quint32 h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    QByteArray message = input;
    const quint64 bitLength = static_cast<quint64>(message.size()) * 8;

    message.append(char(0x80));
    while (message.size() % 64 != 56)
        message.append(char(0x00));

    for (int i = 7; i >= 0; --i)
        message.append(static_cast<char>((bitLength >> (i * 8)) & 0xFF));

    const unsigned char *data = reinterpret_cast<const unsigned char *>(message.constData());
    const int blocks = message.size() / 64;

    for (int b = 0; b < blocks; ++b) {
        quint32 w[80];
        const unsigned char *chunk = data + b * 64;

        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<quint32>(chunk[i * 4]) << 24)
                 | (static_cast<quint32>(chunk[i * 4 + 1]) << 16)
                 | (static_cast<quint32>(chunk[i * 4 + 2]) << 8)
                 | (static_cast<quint32>(chunk[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        quint32 a = h0, b2 = h1, c = h2, d = h3, e = h4;

        for (int i = 0; i < 80; ++i) {
            quint32 f, k;
            if (i < 20)      { f = (b2 & c) | ((~b2) & d);        k = 0x5A827999; }
            else if (i < 40) { f = b2 ^ c ^ d;                    k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b2 & c) | (b2 & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b2 ^ c ^ d;                    k = 0xCA62C1D6; }

            const quint32 temp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b2, 30); b2 = a; a = temp;
        }

        h0 += a; h1 += b2; h2 += c; h3 += d; h4 += e;
    }

    QByteArray digest(20, '\0');
    unsigned char *out = reinterpret_cast<unsigned char *>(digest.data());
    quint32 words[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i) {
        out[i * 4]     = static_cast<unsigned char>((words[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<unsigned char>((words[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<unsigned char>((words[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<unsigned char>(words[i] & 0xFF);
    }
    return digest;
}
