#ifndef WATCHDATAWRITER_H
#define WATCHDATAWRITER_H

#include <QtEndian>
#include <QByteArray>
#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <QLoggingCategory>

class WatchDataWriter
{
public:
    WatchDataWriter(QByteArray *buf);

    template <typename T>
    void write(T v);

    template <typename T>
    void writeLE(T v);

    void writeBytes(int n, const QByteArray &b);

    static QByteArray chopStringToByteLength(const QString &s, int n);

    void writeFixedString(int n, const QString &s);

    void writeCString(const QString &s);

    void writePascalString(const QString &s);

    void writeUuid(const QUuid &uuid);

    void writeDict(const QMap<int, QVariant> &d);

/**
    Name   : "CRC-32"
    Width  : 32
    Poly   : 04C11DB7
    Init   : FFFFFFFF
    RefIn  : True
    RefOut : True
    XorOut : FFFFFFFF
    Check  : CBF43926
http://www.st.com/content/ccc/resource/technical/document/application_note/39/89/da/89/9e/d7/49/b1/DM00068118.pdf/files/DM00068118.pdf/jcr:content/translations/en.DM00068118.pdf
*/
    #define STM_CRC_POLY 0x04C11DB7
    #define STM_CRC_INIT 0xFFFFFFFF
    #define STM_CRC_OXOR 0xFFFFFFFF
    static quint32 stm32crc(const QByteArray &data, quint32 crc = STM_CRC_INIT);

private:
    char *p(int n);
    uchar *up(int n);
    QByteArray *_buf;
};

inline WatchDataWriter::WatchDataWriter(QByteArray *buf)
    : _buf(buf)
{
}

template <typename T>
void WatchDataWriter::write(T v)
{
    qToBigEndian(v, up(sizeof(T)));
}

template <typename T>
void WatchDataWriter::writeLE(T v)
{
    qToLittleEndian(v, up(sizeof(T)));
}

inline char * WatchDataWriter::p(int n)
{
    int size = _buf->size();
    _buf->resize(size + n);
    return &_buf->data()[size];
}

inline uchar * WatchDataWriter::up(int n)
{
    return reinterpret_cast<uchar *>(p(n));
}



#endif
