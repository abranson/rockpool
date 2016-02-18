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

    void writeFixedString(int n, const QString &s);

    void writeCString(const QString &s);

    void writePascalString(const QString &s);

    void writeUuid(const QUuid &uuid);

    void writeDict(const QMap<int, QVariant> &d);

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
