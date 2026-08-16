#ifndef MEERU_STD_ED25519_H
#define MEERU_STD_ED25519_H

#include <QByteArray>

// Standard, RFC 8032 / SHA-512 based Ed25519 — needed for exactly one thing:
// talking to the real BitTorrent DHT (BEP 44), which every other client
// verifies with SHA-512.
//
// Every other signature in Meeru (the local identity, profile, backups,
// invite codes, the P2P handshake) uses IdentityCrypto, which is Monocypher's
// own EdDSA. That is a perfectly sound, audited signature scheme, but it
// hashes with BLAKE2b instead of SHA-512 — Monocypher's header says so
// outright ("EdDSA with curve25519 + BLAKE2b") — which makes it a different,
// non-interoperable scheme from the one every other Ed25519 implementation
// speaks. That is invisible everywhere else in Meeru, since only Meeru
// verifies Meeru's own signatures there. It is not invisible here: a mainline
// DHT node runs libtorrent or similar, hashes with SHA-512, and would reject
// a BLAKE2b-based signature as garbage.
//
// Rather than reimplementing Curve25519 point arithmetic from scratch, this
// reuses Monocypher's audited, constant-time field and group operations
// through the low-level building blocks it exports for exactly this purpose
// (crypto_eddsa_scalarbase, crypto_eddsa_mul_add, crypto_eddsa_reduce,
// crypto_eddsa_trim_scalar, crypto_eddsa_check_equation) and supplies SHA-512
// wherever Monocypher's own crypto_eddsa_sign/check would supply BLAKE2b.
// The result is verified byte-for-bit against the official BEP 44 test
// vectors, seeds included.
namespace StdEd25519 {

// seed is 32 bytes, arbitrary and secret. Deterministic: the same seed always
// produces the same key pair, exactly like Monocypher's own EdDSA.
void keyPair(const QByteArray &seed, QByteArray *publicKey, QByteArray *secretExpanded);

// secretExpanded is the 64-byte scalar||prefix pair produced by keyPair;
// nothing else in Meeru needs to know that shape.
QByteArray sign(const QByteArray &secretExpanded, const QByteArray &publicKey, const QByteArray &message);

bool verify(const QByteArray &signature, const QByteArray &publicKey, const QByteArray &message);

}

#endif
