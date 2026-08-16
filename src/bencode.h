#ifndef MEERU_BENCODE_H
#define MEERU_BENCODE_H

#include <QByteArray>
#include <QList>
#include <QMap>

// Bencode: the encoding used by every message on the BitTorrent DHT (BEP 5).
// A minimal but correct implementation, just enough for KRPC dictionaries and
// BEP 44 values: integers, byte strings, lists, and dictionaries.
//
// Dictionary keys are always written in sorted byte order, which the
// specification requires and which BEP 44 additionally relies on: a value
// whose bencoded dictionary keys are not sorted must be rejected outright,
// since the signature is defined over one specific encoding of it.
class BencodeValue
{
public:
    enum Kind { Integer, String, List, Dictionary, Invalid };

    BencodeValue();
    static BencodeValue fromInt(qint64 value);
    static BencodeValue fromString(const QByteArray &value);
    static BencodeValue fromList(const QList<BencodeValue> &value);
    static BencodeValue fromDict(const QMap<QByteArray, BencodeValue> &value);

    Kind kind() const { return kind_; }
    bool isValid() const { return kind_ != Invalid; }

    qint64 toInt() const { return integer_; }
    QByteArray toByteArray() const { return string_; }
    QList<BencodeValue> toList() const { return list_; }
    QMap<QByteArray, BencodeValue> toDict() const { return dict_; }

    // Dictionary convenience: returns an Invalid value if the key is absent.
    BencodeValue value(const QByteArray &key) const;
    bool contains(const QByteArray &key) const;

    QByteArray encode() const;

private:
    Kind kind_;
    qint64 integer_;
    QByteArray string_;
    QList<BencodeValue> list_;
    QMap<QByteArray, BencodeValue> dict_;
};

namespace Bencode {

QByteArray encode(const BencodeValue &value);

// Parses exactly one value starting at *offset and advances it. Returns an
// Invalid value on any malformed input; never throws and never reads past the
// end of the buffer, since this parses bytes that arrived over UDP from
// strangers.
BencodeValue decode(const QByteArray &data, int *offset);
BencodeValue decode(const QByteArray &data);

}

#endif
