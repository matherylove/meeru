#include "bencode.h"

namespace {
const int kMaxDepth = 24;          // KRPC messages are shallow; anything deeper is hostile
const int kMaxLength = 64 * 1024;  // generous ceiling for a UDP-carried message
}

BencodeValue::BencodeValue()
    : kind_(Invalid), integer_(0)
{
}

BencodeValue BencodeValue::fromInt(qint64 value)
{
    BencodeValue result;
    result.kind_ = Integer;
    result.integer_ = value;
    return result;
}

BencodeValue BencodeValue::fromString(const QByteArray &value)
{
    BencodeValue result;
    result.kind_ = String;
    result.string_ = value;
    return result;
}

BencodeValue BencodeValue::fromList(const QList<BencodeValue> &value)
{
    BencodeValue result;
    result.kind_ = List;
    result.list_ = value;
    return result;
}

BencodeValue BencodeValue::fromDict(const QMap<QByteArray, BencodeValue> &value)
{
    BencodeValue result;
    result.kind_ = Dictionary;
    result.dict_ = value;
    return result;
}

BencodeValue BencodeValue::value(const QByteArray &key) const
{
    if (kind_ != Dictionary)
        return BencodeValue();
    return dict_.value(key, BencodeValue());
}

bool BencodeValue::contains(const QByteArray &key) const
{
    return kind_ == Dictionary && dict_.contains(key);
}

QByteArray BencodeValue::encode() const
{
    return Bencode::encode(*this);
}

QByteArray Bencode::encode(const BencodeValue &value)
{
    switch (value.kind()) {
    case BencodeValue::Integer:
        return QByteArray("i") + QByteArray::number(value.toInt()) + QByteArray("e");

    case BencodeValue::String: {
        const QByteArray bytes = value.toByteArray();
        return QByteArray::number(bytes.size()) + QByteArray(":") + bytes;
    }

    case BencodeValue::List: {
        QByteArray out = "l";
        const QList<BencodeValue> items = value.toList();
        for (int i = 0; i < items.size(); ++i)
            out += encode(items.at(i));
        out += "e";
        return out;
    }

    case BencodeValue::Dictionary: {
        // QMap<QByteArray, ...> iterates in ascending key order already,
        // which is exactly the sort bencode and BEP 44 both require.
        QByteArray out = "d";
        const QMap<QByteArray, BencodeValue> dict = value.toDict();
        QMap<QByteArray, BencodeValue>::const_iterator it = dict.constBegin();
        for (; it != dict.constEnd(); ++it) {
            out += QByteArray::number(it.key().size()) + QByteArray(":") + it.key();
            out += encode(it.value());
        }
        out += "e";
        return out;
    }

    default:
        return QByteArray();
    }
}

namespace {

BencodeValue decodeAt(const QByteArray &data, int *offset, int depth)
{
    if (!offset || *offset < 0 || *offset >= data.size() || depth > kMaxDepth)
        return BencodeValue();

    const char marker = data.at(*offset);

    if (marker == 'i') {
        const int end = data.indexOf('e', *offset + 1);
        if (end < 0 || end - *offset > 32)
            return BencodeValue();
        bool ok = false;
        const qint64 value = data.mid(*offset + 1, end - *offset - 1).toLongLong(&ok);
        if (!ok)
            return BencodeValue();
        *offset = end + 1;
        return BencodeValue::fromInt(value);
    }

    if (marker == 'l') {
        int cursor = *offset + 1;
        QList<BencodeValue> items;
        while (cursor < data.size() && data.at(cursor) != 'e') {
            const BencodeValue item = decodeAt(data, &cursor, depth + 1);
            if (!item.isValid())
                return BencodeValue();
            items.append(item);
        }
        if (cursor >= data.size())
            return BencodeValue();
        *offset = cursor + 1;
        return BencodeValue::fromList(items);
    }

    if (marker == 'd') {
        int cursor = *offset + 1;
        QMap<QByteArray, BencodeValue> dict;
        while (cursor < data.size() && data.at(cursor) != 'e') {
            const BencodeValue key = decodeAt(data, &cursor, depth + 1);
            if (!key.isValid() || key.kind() != BencodeValue::String)
                return BencodeValue();
            const BencodeValue val = decodeAt(data, &cursor, depth + 1);
            if (!val.isValid())
                return BencodeValue();
            dict.insert(key.toByteArray(), val);
        }
        if (cursor >= data.size())
            return BencodeValue();
        *offset = cursor + 1;
        return BencodeValue::fromDict(dict);
    }

    if (marker >= '0' && marker <= '9') {
        const int colon = data.indexOf(':', *offset);
        if (colon < 0 || colon - *offset > 10)
            return BencodeValue();
        bool ok = false;
        const qint64 length = data.mid(*offset, colon - *offset).toLongLong(&ok);
        if (!ok || length < 0 || length > kMaxLength)
            return BencodeValue();
        const int start = colon + 1;
        if (start + length > data.size())
            return BencodeValue();
        *offset = start + static_cast<int>(length);
        return BencodeValue::fromString(data.mid(start, static_cast<int>(length)));
    }

    return BencodeValue();
}

}

BencodeValue Bencode::decode(const QByteArray &data, int *offset)
{
    return decodeAt(data, offset, 0);
}

BencodeValue Bencode::decode(const QByteArray &data)
{
    int offset = 0;
    return decodeAt(data, &offset, 0);
}
