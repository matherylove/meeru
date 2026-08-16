#include "crc32c.h"

namespace {

quint32 table[256];
bool tableReady = false;

void buildTable()
{
    const quint32 polynomial = 0x82F63B78u;   // reversed Castagnoli polynomial
    for (quint32 i = 0; i < 256; ++i) {
        quint32 value = i;
        for (int bit = 0; bit < 8; ++bit)
            value = (value & 1) ? (polynomial ^ (value >> 1)) : (value >> 1);
        table[i] = value;
    }
    tableReady = true;
}

}

quint32 Crc32c::compute(const unsigned char *data, int length)
{
    if (!tableReady)
        buildTable();

    quint32 crc = 0xFFFFFFFFu;
    for (int i = 0; i < length; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
