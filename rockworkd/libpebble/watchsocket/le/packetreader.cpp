#include "packetreader.h"

PacketReader::PacketReader(QObject *parent):
  QIODevice(parent),
  m_data(QByteArray())
{
}

qint64 PacketReader::bytesAvailable() const
{
    return m_data.size() + QIODevice::bytesAvailable();
}

bool PacketReader::isSequential() const
{
    return true;
}

qint64 PacketReader::readData(char* data, qint64 maxSize)
{
    if (m_data.length() == 0 || maxSize <= 0)
        return -1;

    maxSize = m_data.length() > maxSize ? maxSize : m_data.length();
    memcpy(data, m_data.constData(), maxSize);
    m_data.remove(0, maxSize);

    return maxSize;
}

qint64 PacketReader::writeData(const char* data, qint64 maxSize)
{
    qDebug() << "writeData() called!" << maxSize;
    if (m_data.length() == 0)
        m_data.resize(maxSize);

    m_data.append(data);

    return maxSize;
}

void PacketReader::write(const QByteArray &data)
{
    m_data.append(data);
    emit readyRead();
}