#ifndef MEERU_SHA512_H
#define MEERU_SHA512_H

#include <QByteArray>

// SHA-512, needed for exactly one thing: standard Ed25519 signatures for the
// BitTorrent DHT (BEP 44). See std_ed25519.h for why this exists at all —
// Monocypher's own EdDSA uses BLAKE2b instead of SHA-512, which does not
// interoperate with the real DHT or any other standard Ed25519 library.
namespace Sha512 {

QByteArray hash(const QByteArray &input);   // always 64 bytes

}

#endif
