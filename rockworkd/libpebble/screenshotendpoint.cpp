#include "screenshotendpoint.h"

#include "watchdatawriter.h"
#include "watchdatareader.h"
#include "pebble.h"

#include <QImage>
#include <QDateTime>
#include <QDir>

ScreenshotEndpoint::ScreenshotEndpoint(Pebble *pebble, WatchConnection *connection, QObject *parent):
    QObject(parent),
    m_pebble(pebble),
    m_connection(connection)
{
    m_connection->registerEndpointHandler(WatchConnection::EndpointScreenshot, this, "handleScreenshotData");
}

void ScreenshotEndpoint::requestScreenshot()
{
    ScreenshotRequestPackage package;
    m_connection->writeToPebble(WatchConnection::EndpointScreenshot, package.serialize());
}

void ScreenshotEndpoint::removeScreenshot(const QString &filename)
{
    QFile f(filename);
    if (f.exists() && f.remove()) {
        emit screenshotRemoved(filename);
    }
}

QStringList ScreenshotEndpoint::screenshots() const
{
    QDir dir(m_pebble->storagePath() + "/screenshots/");
    QStringList ret;
    foreach (const QString &filename, dir.entryList(QDir::Files)) {
        ret << m_pebble->storagePath() + "/screenshots/" + filename;
    }

    return ret;
}

void ScreenshotEndpoint::handleScreenshotData(const QByteArray &data)
{
    WatchDataReader reader(data);
    int offset = 0;

    if (m_waitingForMore == 0) {

        ResponseCode responseCode = (ResponseCode)reader.read<quint8>();
        if (responseCode != ResponseCodeOK) {
            qWarning() << "Error taking screenshot:" << responseCode;
            return;
        }
        m_version = reader.read<quint32>();

        m_width = reader.read<quint32>();
        m_height = reader.read<quint32>();

        switch (m_version) {
        case 1:
            m_waitingForMore = m_width * m_height / 8;
            break;
        case 2:
            m_waitingForMore = m_width * m_height;
            break;
        default:
            qWarning() << "Unsupported screenshot format version";
            m_waitingForMore = m_width * m_height; // might work :)
        }

        offset = 13;
        m_accumulatedData.clear();
    }

    QByteArray tmp = reader.readBytes(data.length() - offset);
    m_waitingForMore -= tmp.length();
    m_accumulatedData.append(tmp);

    if (m_waitingForMore == 0) {
        QByteArray output;
        switch (m_version) {
        case 1: {
            int rowBytes = m_width / 8;
            for (quint32 row = 0; row < m_height; row++) {
                for (quint32 col = 0; col < m_width; col++) {
                    char pixel = (m_accumulatedData.at(row * rowBytes + col / 8) >> (col % 8)) & 1;
                    output.append(pixel * 255);
                    output.append(pixel * 255);
                    output.append(pixel * 255);
                }
            }
            break;
        }
        case 2:
            for (quint32 row = 0; row < m_height; row++) {
                for (quint32 col = 0; col < m_width; col++) {
                    char pixel = m_accumulatedData.at(row * m_width + col);
                    output.append(((pixel >> 4) & 0b11) * 85);
                    output.append(((pixel >> 2) & 0b11) * 85);
                    output.append(((pixel >> 0) & 0b11) * 85);
                }
            }
            break;
        default:
            qWarning() << "Invalid format.";
            return;
        }

        QImage image = QImage((uchar*)output.data(), m_width, m_height, QImage::Format_RGB888);
        QDir dir(m_pebble->storagePath() + "/screenshots/");
        if (!dir.exists()) {
            dir.mkpath(dir.absolutePath());
        }
        QString filename = dir.absolutePath() + "/" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss") + ".jpg";
        image.save(filename);
        qDebug() << "Screenshot saved to" << filename;
        emit screenshotAdded(filename);
    }
}


QByteArray ScreenshotRequestPackage::serialize() const
{
    QByteArray data;
    WatchDataWriter writer(&data);

    writer.write<quint8>(m_command);
    return data;
}
