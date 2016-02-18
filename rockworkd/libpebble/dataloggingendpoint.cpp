#include "dataloggingendpoint.h"

#include "pebble.h"
#include "watchconnection.h"
#include "watchdatareader.h"
#include "watchdatawriter.h"

DataLoggingEndpoint::DataLoggingEndpoint(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection)
{
    m_connection->registerEndpointHandler(WatchConnection::EndpointDataLogging, this, "handleMessage");
}

void DataLoggingEndpoint::handleMessage(const QByteArray &data)
{
    qDebug() << "data logged" << data.toHex();
    WatchDataReader reader(data);
    DataLoggingCommand command = (DataLoggingCommand)reader.read<quint8>();
    switch (command) {
    case DataLoggingDespoolSendData: {
        quint8 sessionId = reader.read<quint8>();
        quint32 itemsLeft = reader.readLE<quint32>();
        quint32 crc = reader.readLE<quint32>();
        qDebug() << "Despooling data: Session:" << sessionId << "Items left:" << itemsLeft << "CRC:" << crc;

        QByteArray reply;
        WatchDataWriter writer(&reply);
        writer.write<quint8>(DataLoggingACK);
        writer.write<quint8>(sessionId);
        m_connection->writeToPebble(WatchConnection::EndpointDataLogging, reply);
        return;
    }
    case DataLoggingTimeout: {
        quint8 sessionId = reader.read<quint8>();
        qDebug() << "DataLogging reached timeout: Session:" << sessionId;
        return;
    }
    default:
        qDebug() << "Unhandled DataLogging message";
    }
}

