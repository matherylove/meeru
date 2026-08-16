#ifndef MEERU_CRC32C_H
#define MEERU_CRC32C_H

#include <QtGlobal>

// CRC32C (Castagnoli), used only for BEP 42: the DHT node ID security
// extension that ties a node's chosen ID to its external IP address, making
// it harder to cluster malicious nodes around a specific target.
namespace Crc32c {

quint32 compute(const unsigned char *data, int length);

}

#endif
