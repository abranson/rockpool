#include "watchdatawriter.h"
#include "watchconnection.h"

void WatchDataWriter::writeBytes(int n, const QByteArray &b)
{
    if (b.size() > n) {
        _buf->append(b.constData(), n);
    } else {
        int diff = n - b.size();
        _buf->append(b);
        if (diff > 0) {
            _buf->append(QByteArray(diff, '\0'));
        }
    }
}

QByteArray WatchDataWriter::chopStringToByteLength(const QString &s, int n) {

    int charLen = n;
    QByteArray utf8Bytes = s.left(charLen).toUtf8();
    while (utf8Bytes.length() > n) {
        utf8Bytes = s.left(--charLen).toUtf8();
    }
    return utf8Bytes;
}

void WatchDataWriter::writeFixedString(int n, const QString &s)
{
    QByteArray utf8Bytes = chopStringToByteLength(s, n-1);
    _buf->append(utf8Bytes);
    for (int i = utf8Bytes.length(); i < n; i++) {
        _buf->append('\0');
    }
}

void WatchDataWriter::writeCString(const QString &s)
{
    _buf->append(s.toUtf8());
    _buf->append('\0');
}

void WatchDataWriter::writePascalString(const QString &s)
{
    QByteArray utf8bytes = chopStringToByteLength(s, 0xFE);
    int length = utf8bytes.length()+1;
    _buf->append(length);
    _buf->append(utf8bytes);
    _buf->append('\0');
}

void WatchDataWriter::writeUuid(const QUuid &uuid)
{
    writeBytes(16, uuid.toRfc4122());
}

void WatchDataWriter::writeDict(const QMap<int, QVariant> &d)
{
    int size = d.size();
    if (size > 0xFF) {
        qWarning() << "Dictionary is too large to encode";
        writeLE<quint8>(0);
        return;
    }

    writeLE<quint8>(size);

    for (QMap<int, QVariant>::const_iterator it = d.constBegin(); it != d.constEnd(); ++it) {

        switch (int(it.value().type())) {
        case QMetaType::VoidStar: // skip nulls
            continue;
        case QMetaType::Char:
            writeLE<quint32>(it.key());
            writeLE<quint8>(WatchConnection::DictItemTypeInt);
            writeLE<quint16>(sizeof(char));
            writeLE<char>(it.value().value<char>());
            break;
        case QMetaType::Short:
            writeLE<quint32>(it.key());
            writeLE<quint8>(WatchConnection::DictItemTypeInt);
            writeLE<quint16>(sizeof(short));
            writeLE<short>(it.value().value<short>());
            break;
        case QMetaType::Int:
            writeLE<quint32>(it.key());
            writeLE<quint8>(WatchConnection::DictItemTypeInt);
            writeLE<quint16>(sizeof(int));
            writeLE<int>(it.value().value<int>());
            break;

        case QMetaType::UChar:
            writeLE<quint32>(it.key());
            writeLE<quint8>(WatchConnection::DictItemTypeInt);
            writeLE<quint16>(sizeof(char));
            writeLE<char>(it.value().value<char>());
            break;
        case QMetaType::SChar:
            writeLE<quint32>(it.key());
            writeLE<quint8>(WatchConnection::DictItemTypeInt);
            writeLE<quint16>(sizeof(signed char));
            writeLE<signed char>(it.value().value<signed char>());
            break;
        case QMetaType::UShort:
            writeLE<quint32>(it.key());
            writeLE<quint8>(WatchConnection::DictItemTypeUInt);
            writeLE<quint16>(sizeof(ushort));
            writeLE<ushort>(it.value().value<ushort>());
            break;
        case QMetaType::UInt:
            writeLE<quint32>(it.key());
            writeLE<quint8>(WatchConnection::DictItemTypeUInt);
            writeLE<quint16>(sizeof(uint));
            writeLE<uint>(it.value().value<uint>());
            break;

        case QMetaType::Bool:
            writeLE<quint32>(it.key());
            writeLE<quint8>(WatchConnection::DictItemTypeInt);
            writeLE<quint16>(sizeof(char));
            writeLE<char>(it.value().value<char>());
            break;

        case QMetaType::Float: // Treat qreals as ints
        case QMetaType::Double:
            writeLE<quint32>(it.key());
            writeLE<quint8>(WatchConnection::DictItemTypeInt);
            writeLE<quint16>(sizeof(int));
            writeLE<int>(it.value().value<int>());
            break;

        case QMetaType::QByteArray: {
            writeLE<quint32>(it.key());
            QByteArray ba = it.value().toByteArray();
            writeLE<quint8>(WatchConnection::DictItemTypeBytes);
            writeLE<quint16>(ba.size());
            _buf->append(ba);
            break;
        }

        case QMetaType::QVariantList: {
            // Generally a JS array, which we marshal as a byte array.
            writeLE<quint32>(it.key());
            QVariantList list = it.value().toList();
            QByteArray ba;
            ba.reserve(list.size());

            Q_FOREACH (const QVariant &v, list) {
                ba.append(v.toInt());
            }

            writeLE<quint8>(WatchConnection::DictItemTypeBytes);
            writeLE<quint16>(ba.size());
            _buf->append(ba);
            break;
        }

        default:
            qWarning() << "Unknown dict item type:" << it.value().typeName();
            /* Fallthrough */
        case QMetaType::QString:
        case QMetaType::QUrl:
        {
            writeLE<quint32>(it.key());
            QByteArray s = it.value().toString().toUtf8();
            if (s.isEmpty() || s[s.size() - 1] != '\0') {
                // Add null terminator if it doesn't have one
                s.append('\0');
            }
            writeLE<quint8>(WatchConnection::DictItemTypeString);
            writeLE<quint16>(s.size());
            _buf->append(s);
            break;
        }
        }
    }
}

quint32 WatchDataWriter::stm32crc(const QByteArray &buf, quint32 crc) {
    for(int i=0;i<buf.length();i+=4) {
        QByteArray dw = buf.mid(i,4);
        if(dw.length()<4) {
            for(int j=dw.length();j<4;j++)
                dw.prepend('\0');
        }
        quint32 dwuint = *(quint32*)dw.constData();
        crc ^= dwuint;
        for(int j=0;j<32;j++) {
            crc = (crc & 0x80000000) ? ((crc << 1) ^ STM_CRC_POLY) : (crc << 1);
        }
        crc &= STM_CRC_OXOR;
    }
    return crc;
}
