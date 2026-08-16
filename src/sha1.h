#ifndef MEERU_SHA1_H
#define MEERU_SHA1_H

#include <QByteArray>

// SHA-1, needed only for interoperating with the BitTorrent DHT: BEP 44 keys
// items by the SHA-1 hash of a public key (plus an optional salt), and BEP 5
// write tokens are conventionally SHA-1 of the requester's IP and a secret.
// Monocypher deliberately does not offer SHA-1 since nothing else in Meeru
// needs it or should use it; this is a small, self-contained implementation
// used nowhere except the DHT.
namespace Sha1 {

QByteArray hash(const QByteArray &input);   // always 20 bytes

}

#endif
