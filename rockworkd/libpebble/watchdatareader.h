#ifndef WATCHDATAREADER_H
#define WATCHDATAREADER_H

#include "watchconnection.h"

#include <QByteArray>
#include <QtEndian>
#include <QString>
#include <QUuid>
#include <QMap>

class WatchDataReader {
public:
    WatchDataReader(const QByteArray &data):
        m_data(data)
    {
    }

    template <typename T>
    T read() {
        if (checkBad(sizeof(T))) return 0;
        const uchar *u = p();
        m_offset += sizeof(T);
        return qFromBigEndian<T>(u);
    }

    inline bool checkBad(int n = 0)
    {
        if (m_offset + n > m_data.size()) {
            m_bad = true;
        }
        return m_bad;
    }
    inline const uchar * p()
    {
        return reinterpret_cast<const uchar *>(&m_data.constData()[m_offset]);
    }
    inline void skip(int n)
    {
        m_offset += n;
        checkBad();
    }

    template <typename T>
    inline T readLE()
    {
        if (checkBad(sizeof(T))) return 0;
        const uchar *u = p();
        m_offset += sizeof(T);
        return qFromLittleEndian<T>(u);
    }
    QString readFixedString(int n)
    {
        if (checkBad(n)) return QString();
        const char *u = &m_data.constData()[m_offset];
        m_offset += n;
        return QString::fromUtf8(u, strnlen(u, n));
    }
    QByteArray peek(int n) {
        return m_data.left(m_offset + n).right(n);
    }
    QUuid readUuid()
    {
        if (checkBad(16)) return QString();
        m_offset += 16;
        return QUuid::fromRfc4122(m_data.mid(m_offset - 16, 16));
    }
    QByteArray readBytes(int n)
    {
        if (checkBad(n)) return QByteArray();
        const char *u = &m_data.constData()[m_offset];
        m_offset += n;
        return QByteArray(u, n);
    }
    QMap<int, QVariant> readDict()
    {
        QMap<int, QVariant> d;
        if (checkBad(1)) return d;

        const int n = readLE<quint8>();

        for (int i = 0; i < n; i++) {
            if (checkBad(4 + 1 + 2)) return d;
            const int key = readLE<qint32>(); // For some reason, this is little endian.
            const int type = readLE<quint8>();
            const int width = readLE<quint16>();

            switch (type) {
            case WatchConnection::DictItemTypeBytes:
                d.insert(key, QVariant::fromValue(readBytes(width)));
                break;
            case WatchConnection::DictItemTypeString:
                d.insert(key, QVariant::fromValue(readFixedString(width)));
                break;
            case WatchConnection::DictItemTypeUInt:
                switch (width) {
                case sizeof(quint8):
                    d.insert(key, QVariant::fromValue(readLE<quint8>()));
                    break;
                case sizeof(quint16):
                    d.insert(key, QVariant::fromValue(readLE<quint16>()));
                    break;
                case sizeof(quint32):
                    d.insert(key, QVariant::fromValue(readLE<quint32>()));
                    break;
                default:
                    m_bad = true;
                    return d;
                }

                break;
            case WatchConnection::DictItemTypeInt:
                switch (width) {
                case sizeof(qint8):
                    d.insert(key, QVariant::fromValue(readLE<qint8>()));
                    break;
                case sizeof(qint16):
                    d.insert(key, QVariant::fromValue(readLE<qint16>()));
                    break;
                case sizeof(qint32):
                    d.insert(key, QVariant::fromValue(readLE<qint32>()));
                    break;
                default:
                    m_bad = true;
                    return d;
                }

                break;
            default:
                m_bad = true;
                return d;
            }
        }

        return d;
    }
    bool bad() const;


private:
    QByteArray m_data;
    int m_offset = 0;
    bool m_bad = false;
};

#endif // WATCHDATAREADER_H
