#include "peer_session.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTimer>

#include "monocypher.h"

namespace {

const char kMagic[] = { 'M', 'E', 'E', 'R', 'U', 'P', '2', 'P' };
const int kMagicSize = 8;
const quint8 kVersion = 1;
const int kKeySize = 32;
const int kSignatureSize = 64;
const int kMacSize = 16;
const int kNonceSize = 24;
const int kHelloSize = kMagicSize + 1 + kKeySize * 3 + 16;   // magic, version, ed, x, ephemeral, salt
const int kMaxFrame = 4 * 1024 * 1024;
const int kHandshakeTimeoutMs = 20000;

void appendU32(QByteArray *out, quint32 value)
{
    char buffer[4];
    buffer[0] = static_cast<char>(value & 0xFF);
    buffer[1] = static_cast<char>((value >> 8) & 0xFF);
    buffer[2] = static_cast<char>((value >> 16) & 0xFF);
    buffer[3] = static_cast<char>((value >> 24) & 0xFF);
    out->append(buffer, 4);
}

quint32 readU32(const QByteArray &in, int offset)
{
    const unsigned char *data = reinterpret_cast<const unsigned char *>(in.constData()) + offset;
    return static_cast<quint32>(data[0])
         | (static_cast<quint32>(data[1]) << 8)
         | (static_cast<quint32>(data[2]) << 16)
         | (static_cast<quint32>(data[3]) << 24);
}

QByteArray blake(const QByteArray &input, int size)
{
    QByteArray out(size, '\0');
    crypto_blake2b(reinterpret_cast<unsigned char *>(out.data()), static_cast<size_t>(size),
                   reinterpret_cast<const unsigned char *>(input.constData()),
                   static_cast<size_t>(input.size()));
    return out;
}

QByteArray counterNonce(quint64 counter)
{
    QByteArray nonce(kNonceSize, '\0');
    for (int i = 0; i < 8; ++i)
        nonce[kNonceSize - 8 + i] = static_cast<char>((counter >> (8 * i)) & 0xFF);
    return nonce;
}

}

PeerSession::PeerSession(QTcpSocket *socket,
                         const IdentityMaterial &material,
                         const QString &ownIdentityId,
                         bool initiator,
                         const QString &expectedPeerId,
                         QObject *parent)
    : QObject(parent),
      socket_(socket),
      ownId_(ownIdentityId),
      expectedPeerId_(expectedPeerId),
      initiator_(initiator),
      stage_(WaitingHello),
      sendCounter_(0),
      receiveCounter_(0),
      timeout_(0)
{
    material_ = material;

    socket_->setParent(this);
    socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(socket_, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
    connect(socket_, SIGNAL(disconnected()), this, SLOT(onDisconnected()));

    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    timeout_->setInterval(kHandshakeTimeoutMs);
    connect(timeout_, SIGNAL(timeout()), this, SLOT(onTimeout()));
    timeout_->start();

}

void PeerSession::begin()
{
    if (stage_ != WaitingHello)
        return;

    sendHello();

    // Bytes may already be waiting if the peer wrote before we adopted the
    // socket; readyRead would never fire for those.
    if (socket_ && socket_->bytesAvailable() > 0)
        onReadyRead();
}

PeerSession::~PeerSession()
{
    material_.clear();
    IdentityCrypto::wipe(&ephemeralSecret_);
    IdentityCrypto::wipe(&sendKey_);
    IdentityCrypto::wipe(&receiveKey_);
}

QString PeerSession::peerAddress() const
{
    if (!socket_)
        return QString();
    return socket_->peerAddress().toString() + QLatin1Char(':') + QString::number(socket_->peerPort());
}

void PeerSession::sendHello()
{
    QByteArray ephemeral;
    QByteArray salt;
    if (!IdentityCrypto::randomBytes(&ephemeral, kKeySize) || !IdentityCrypto::randomBytes(&salt, 16)) {
        fail(QString::fromLatin1("No secure random source"));
        return;
    }
    ephemeralSecret_ = ephemeral;

    unsigned char ephemeralPublic[kKeySize];
    crypto_x25519_public_key(ephemeralPublic,
                             reinterpret_cast<const unsigned char *>(ephemeralSecret_.constData()));

    ownHello_ = QByteArray(kMagic, kMagicSize);
    ownHello_.append(static_cast<char>(kVersion));
    ownHello_.append(material_.edPublic);
    ownHello_.append(material_.xPublic);
    ownHello_.append(reinterpret_cast<char *>(ephemeralPublic), kKeySize);
    ownHello_.append(salt);
    crypto_wipe(ephemeralPublic, sizeof(ephemeralPublic));

    writeFrame(ownHello_);
}

bool PeerSession::handleHello(const QByteArray &frame)
{
    if (frame.size() != kHelloSize) {
        fail(QString::fromLatin1("Malformed greeting"));
        return false;
    }
    if (frame.left(kMagicSize) != QByteArray(kMagic, kMagicSize)) {
        fail(QString::fromLatin1("Not a Meeru peer"));
        return false;
    }
    if (static_cast<quint8>(frame.at(kMagicSize)) != kVersion) {
        fail(QString::fromLatin1("That peer speaks a different Meeru version"));
        return false;
    }

    peerHello_ = frame;
    peerEdPublic_ = frame.mid(kMagicSize + 1, kKeySize);

    // The ID is derived from the key, so this is what stops anyone claiming
    // somebody else's ID.
    peerId_ = IdentityCrypto::identityIdFor(peerEdPublic_);
    if (peerId_ == ownId_) {
        fail(QString::fromLatin1("That is this identity"));
        return false;
    }
    if (!expectedPeerId_.isEmpty() && peerId_ != expectedPeerId_) {
        fail(QString::fromLatin1("The peer answering is not the one we asked for"));
        return false;
    }

    deriveKeys();

    const QByteArray signature = IdentityCrypto::profileSignature(material_, transcript_);
    if (signature.size() != kSignatureSize) {
        fail(QString::fromLatin1("Cannot sign the handshake"));
        return false;
    }

    stage_ = WaitingAuth;
    writeFrame(signature);
    material_.clear();
    return true;
}

void PeerSession::deriveKeys()
{
    const QByteArray &initiatorHello = initiator_ ? ownHello_ : peerHello_;
    const QByteArray &responderHello = initiator_ ? peerHello_ : ownHello_;

    QByteArray transcriptInput = QByteArray("meeru-p2p-v1");
    transcriptInput.append(initiatorHello);
    transcriptInput.append(responderHello);
    transcript_ = blake(transcriptInput, 32);

    const QByteArray peerEphemeral = peerHello_.mid(kMagicSize + 1 + kKeySize * 2, kKeySize);

    unsigned char shared[kKeySize];
    crypto_x25519(shared,
                  reinterpret_cast<const unsigned char *>(ephemeralSecret_.constData()),
                  reinterpret_cast<const unsigned char *>(peerEphemeral.constData()));

    QByteArray keyInput = QByteArray("meeru-p2p-keys");
    keyInput.append(reinterpret_cast<char *>(shared), kKeySize);
    keyInput.append(transcript_);
    const QByteArray keys = blake(keyInput, 64);

    crypto_wipe(shared, sizeof(shared));
    IdentityCrypto::wipe(&keyInput);
    IdentityCrypto::wipe(&ephemeralSecret_);

    // First half travels initiator to responder, second half the other way.
    if (initiator_) {
        sendKey_ = keys.left(kKeySize);
        receiveKey_ = keys.mid(kKeySize, kKeySize);
    } else {
        sendKey_ = keys.mid(kKeySize, kKeySize);
        receiveKey_ = keys.left(kKeySize);
    }
}

bool PeerSession::handleAuth(const QByteArray &frame)
{
    if (frame.size() != kSignatureSize) {
        fail(QString::fromLatin1("Malformed proof of identity"));
        return false;
    }
    if (!IdentityCrypto::verifySignature(peerEdPublic_, transcript_, frame)) {
        fail(QString::fromLatin1("That peer could not prove it owns its Meeru ID"));
        return false;
    }

    stage_ = Established;
    timeout_->stop();
    emit established(peerId_);
    return true;
}

bool PeerSession::handleMessage(const QByteArray &frame)
{
    if (frame.size() < kMacSize) {
        fail(QString::fromLatin1("Truncated message"));
        return false;
    }

    const QByteArray mac = frame.right(kMacSize);
    const QByteArray cipher = frame.left(frame.size() - kMacSize);
    const QByteArray nonce = counterNonce(receiveCounter_);

    QByteArray plain(cipher.size(), '\0');
    const int result = crypto_aead_unlock(
        plain.isEmpty() ? 0 : reinterpret_cast<unsigned char *>(plain.data()),
        reinterpret_cast<const unsigned char *>(mac.constData()),
        reinterpret_cast<const unsigned char *>(receiveKey_.constData()),
        reinterpret_cast<const unsigned char *>(nonce.constData()),
        0, 0,
        cipher.isEmpty() ? 0 : reinterpret_cast<const unsigned char *>(cipher.constData()),
        static_cast<size_t>(cipher.size()));

    if (result != 0) {
        fail(QString::fromLatin1("A message failed its integrity check"));
        return false;
    }
    ++receiveCounter_;

    if (plain.size() < 4) {
        fail(QString::fromLatin1("Malformed message"));
        return false;
    }
    const quint32 headerSize = readU32(plain, 0);
    if (static_cast<int>(headerSize) > plain.size() - 4) {
        fail(QString::fromLatin1("Malformed message"));
        return false;
    }

    const QByteArray header = plain.mid(4, static_cast<int>(headerSize));
    const QByteArray blob = plain.mid(4 + static_cast<int>(headerSize));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(header, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QString::fromLatin1("Malformed message"));
        return false;
    }

    emit controlReceived(peerId_, document.object(), blob);
    return true;
}

void PeerSession::sendControl(const QJsonObject &object)
{
    sendPayload(object, QByteArray());
}

void PeerSession::sendPayload(const QJsonObject &header, const QByteArray &blob)
{
    if (stage_ != Established)
        return;

    const QByteArray headerBytes = QJsonDocument(header).toJson(QJsonDocument::Compact);

    QByteArray plain;
    appendU32(&plain, static_cast<quint32>(headerBytes.size()));
    plain.append(headerBytes);
    plain.append(blob);

    if (plain.size() + kMacSize > kMaxFrame)
        return;

    const QByteArray nonce = counterNonce(sendCounter_);
    QByteArray cipher(plain.size(), '\0');
    unsigned char mac[kMacSize];
    crypto_aead_lock(cipher.isEmpty() ? 0 : reinterpret_cast<unsigned char *>(cipher.data()),
                     mac,
                     reinterpret_cast<const unsigned char *>(sendKey_.constData()),
                     reinterpret_cast<const unsigned char *>(nonce.constData()),
                     0, 0,
                     plain.isEmpty() ? 0 : reinterpret_cast<const unsigned char *>(plain.constData()),
                     static_cast<size_t>(plain.size()));
    ++sendCounter_;

    QByteArray frame = cipher;
    frame.append(reinterpret_cast<char *>(mac), kMacSize);
    crypto_wipe(mac, sizeof(mac));
    writeFrame(frame);
}

bool PeerSession::writeFrame(const QByteArray &data)
{
    if (!socket_ || stage_ == Dead)
        return false;

    QByteArray out;
    appendU32(&out, static_cast<quint32>(data.size()));
    out.append(data);
    return socket_->write(out) == out.size();
}

void PeerSession::onReadyRead()
{
    if (!socket_ || stage_ == Dead)
        return;

    inbox_.append(socket_->readAll());

    while (inbox_.size() >= 4) {
        const quint32 length = readU32(inbox_, 0);
        if (length > static_cast<quint32>(kMaxFrame)) {
            fail(QString::fromLatin1("That peer sent an oversized message"));
            return;
        }
        if (inbox_.size() < static_cast<int>(length) + 4)
            return;

        const QByteArray frame = inbox_.mid(4, static_cast<int>(length));
        inbox_.remove(0, 4 + static_cast<int>(length));

        bool ok = false;
        switch (stage_) {
        case WaitingHello:   ok = handleHello(frame); break;
        case WaitingAuth:    ok = handleAuth(frame); break;
        case Established:    ok = handleMessage(frame); break;
        default:             return;
        }
        if (!ok)
            return;
    }
}

void PeerSession::onDisconnected()
{
    if (stage_ == Dead)
        return;
    stage_ = Dead;
    emit failed(peerId_, QString::fromLatin1("The connection closed"));
}

void PeerSession::onTimeout()
{
    if (stage_ != Established)
        fail(QString::fromLatin1("The peer did not finish the handshake"));
}

void PeerSession::closeSession()
{
    stage_ = Dead;
    if (socket_)
        socket_->disconnectFromHost();
}

void PeerSession::fail(const QString &reason)
{
    if (stage_ == Dead)
        return;
    stage_ = Dead;
    if (socket_)
        socket_->abort();
    emit failed(peerId_, reason);
}
