#include "sendtextapp.h"

#include "blobdb.h"
#include "timelineitem.h"

#include "platforminterface.h"

#include <libintl.h>

const QUuid SendTextApp::actionUUID = QUuid("0f71aaba-5814-4b5c-96e2-c9828c9734cb");
const QUuid SendTextApp::appUUID = QUuid("0863fc6a-66c5-4f62-ab8a-82ed00a98b5d");
const char* SendTextApp::appName = gettext("Send Text");
const QSet<const QByteArray> SendTextApp::appKeys = {{"com.pebble.sendText","com.pebble.android.phone"}};

/**
 * @brief The BlobTextMessage class
 */
class BlobTextMessage : public BlobDbItem
{
public:
    BlobTextMessage(const QString &key, const QStringList &messages, quint8 attrId = 8);

    QByteArray itemKey() const override {return m_key;}
    QByteArray serialize() const override;
private:
    QByteArray m_key;
    quint32 m_flags = 0;
    quint8 m_attCnt = 0;
    quint8 m_actCnt = 1;
    TimelineAction m_action;
};
BlobTextMessage::BlobTextMessage(const QString &key, const QStringList &messages, quint8 attrId):
    BlobDbItem(),
    m_key(key.toUtf8()),
    m_action(0,TimelineAction::TypeResponse)
{
    TimelineAttribute att(attrId);
    att.setStringList(messages);
    m_action.appendAttribute(att);
}
QByteArray BlobTextMessage::serialize() const
{
    QByteArray ret;
    //WatchDataWriter w(&ret);
    //w.writeLE<quint32>(m_flags);
    // till above is clarified/confirmed - below makes more sense
    ret.fill(0,4);
    ret.append(m_attCnt);
    ret.append(m_actCnt);
    ret.append(m_action.serialize());
    return ret;
}

/**
 * @brief The BlobContacts class
 */
class BlobContacts : public BlobDbItem
{
public:
    static const int maxContacts = 10;
    static const QByteArray blobKey;
    struct ContactEntry {
        QUuid contactId;
        QUuid methodId;
        quint8 methodType = 1;
    };

    BlobContacts():BlobDbItem() {}
    BlobContacts(const QList<ContactEntry> &contacts):
        BlobDbItem()
    {
        setEntries(contacts);
    }
    void setEntries(const QList<ContactEntry> &contacts) {
        m_contacts = (contacts.length() < maxContacts) ? contacts : contacts.mid(0,maxContacts);
    }
    void append(const ContactEntry &entry) {
        if(m_contacts.length()<maxContacts)
            m_contacts.append(entry);
    }
    void append(const QList<ContactEntry> &entries) {
        if(m_contacts.length()+entries.length()>maxContacts) return;
        m_contacts.append(entries);
    }

    QByteArray itemKey() const override {return blobKey;}
    QByteArray serialize() const override;
private:
    QList<ContactEntry> m_contacts;
};
const QByteArray BlobContacts::blobKey = QByteArray("sendTextApp",11);

QByteArray BlobContacts::serialize() const
{
    QByteArray ret;
    ret.append(m_contacts.length());
    for(QList<ContactEntry>::const_iterator it=m_contacts.begin();it!=m_contacts.end();it++) {
        ret.append(it->contactId.toRfc4122());
        ret.append(it->methodId.toRfc4122());
        ret.append(it->methodType);
    }
    ret.append(QByteArray((maxContacts-m_contacts.length())*33,'\0'));
    return ret;
}

/**
 * @brief The BlobContact class
 */
class BlobContact : public BlobDbItem
{
public:
    BlobContact(const QString &name, const QStringList &numbers);
    BlobContact(const SendTextApp::Contact &contact):BlobContact(contact.name,contact.numbers) {}

    QList<BlobContacts::ContactEntry> getEntries() const;

    QByteArray itemKey() const override {return m_key.toRfc4122();}
    QByteArray serialize() const override;
private:
    QUuid m_key;
    quint32 m_flags = 0;
    quint8 m_attCnt = 1;
    quint8 m_mthCnt = 0;
    TimelineAttribute m_title;
    // This is mere speculation, only QUuid and Strlen(quint16)+Str(char[Strlen]) are known. AttributeId=0x27 - doesn't exist in layouts.json
    struct ContactMethod {
        QUuid methodId;
        quint8 methodType = 1;
        quint8 methodNum = 1;
        TimelineAttribute method;
    };
    QList<ContactMethod> m_methods;
};

BlobContact::BlobContact(const QString &name, const QStringList &numbers):
    BlobDbItem(),
    m_key(PlatformInterface::idToGuid(name)),
    m_mthCnt(numbers.length())
{
    m_title = TimelineAttribute(1,name);
    foreach (const QString &num, numbers) {
        TimelineAttribute att(0x27);
        att.setString(num);
        ContactMethod mtd;
        mtd.methodId = PlatformInterface::idToGuid(num);
        mtd.method = att;
        m_methods.append(mtd);
    }
}

QList<BlobContacts::ContactEntry> BlobContact::getEntries() const
{
    QList<BlobContacts::ContactEntry> ret;
    for(int i=0;i<m_methods.length();i++) {
        BlobContacts::ContactEntry e;
        e.contactId = m_key;
        e.methodId = m_methods.at(i).methodId;
        e.methodType = m_methods.at(i).methodType;
        ret.append(e);
    }
    return ret;
}

QByteArray BlobContact::serialize() const
{
    QByteArray ret;
    ret.fill('\0',4);
    ret.prepend(m_key.toRfc4122());
    ret.append(m_attCnt);
    ret.append(m_mthCnt);
    ret.append(m_title.serialize());
    foreach(const ContactMethod &m, m_methods) {
        ret.append(m.methodId.toRfc4122());
        ret.append(m.methodType);
        ret.append(m.methodNum);
        ret.append(m.method.serialize());
    }
    return ret;
}

/**
 * @brief SendTextApp::SendTextApp
 * @param pebble
 * @param connection
 */
SendTextApp::SendTextApp(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection)
{
    connect(pebble->blobdb(), &BlobDB::blobCommandResult, this, &SendTextApp::blobdbAckHandler);
}

void SendTextApp::setCannedMessages(const QHash<QString, QStringList> &cans)
{
    foreach (const QString &key, cans.keys()) {
        if(appKeys.contains(key.toUtf8())) {
            qDebug() << "Inserting" << key << cans.value(key);
            m_pebble->blobdb()->insert(BlobDB::blobDBIdSendTextData,BlobTextMessage(key,cans.value(key)));
            m_messages.insert(key.toUtf8(),cans.value(key));
        }
    }
}

void SendTextApp::wipeCannedMessages()
{
    m_pebble->blobdb()->clear(BlobDB::blobDBIdSendTextData);
    m_messages.clear();
}

QStringList SendTextApp::getCannedMessages(const QByteArray &key) const
{
    return m_messages.value(key);
}

void SendTextApp::setCannedContacts(const QList<Contact> &favs)
{
    BlobContacts bcs;
    m_contacts.clear();
    for(QList<Contact>::const_iterator it=favs.begin(); it!=favs.end(); it++) {
        BlobContact bc(*it);
        if(m_contacts.length()>=10) break;
        m_pebble->blobdb()->insert(BlobDB::BlobDBIdContacts,bc);
        bcs.append(bc.getEntries());
        m_contacts.append(*it);
        qDebug() << "Adding contacts" << it->name << it->numbers;// << bc.serialize().toHex();
    }
    //qDebug() << "Inserting config blob" << bcs.serialize().toHex();
    m_pebble->blobdb()->insert(BlobDB::BlobDBIdAppConfigs,bcs);
}

void SendTextApp::wipeContacts()
{
    m_pebble->blobdb()->remove(BlobDB::BlobDBIdAppConfigs,BlobContacts::blobKey);
    m_pebble->blobdb()->clear(BlobDB::BlobDBIdContacts);
    m_contacts.clear();
}

QList<SendTextApp::Contact> SendTextApp::getCannedContacts() const
{
    return m_contacts;
}

void SendTextApp::blobdbAckHandler(quint8 db, quint8 cmd, const QByteArray &key, quint8 ack)
{
    switch(db) {
    case BlobDB::BlobDBIdAppConfigs:
    case BlobDB::BlobDBIdContacts:
    case BlobDB::blobDBIdSendTextData:
        break;
    default:
        return;
    }
    if(db==BlobDB::BlobDBIdAppConfigs) {
        if(key != BlobContacts::blobKey) return;
        if(ack == BlobDB::StatusSuccess)
            emit contactBlobSet();
    } else if(db==BlobDB::blobDBIdSendTextData) {
        if(ack==BlobDB::StatusSuccess)
            emit messageBlobSet(key);
    }
    qDebug() << "BlobDb result for" << db << cmd << ack << key.toHex();
}
