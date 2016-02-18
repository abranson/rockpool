#include "callchannelobserver.h"

#include <TelepathyQt/Contact>
#include <TelepathyQt/PendingContactInfo>

#include <QContactFetchRequest>
#include <QContactPhoneNumber>
#include <QContactFilter>
#include <QContactDetail>
#include <QContactDisplayLabel>

QTCONTACTS_USE_NAMESPACE

TelepathyMonitor::TelepathyMonitor(QObject *parent):
    QObject(parent)
{
    Tp::registerTypes();
    QTimer::singleShot(0, this, &TelepathyMonitor::accountManagerSetup);
    m_contactManager = new QContactManager("galera");
    m_contactManager->setParent(this);
}

void TelepathyMonitor::hangupCall(uint cookie)
{
    if (m_currentCalls.contains(cookie)) {
        m_currentCalls.value(cookie)->hangup();
    }
}

void TelepathyMonitor::accountManagerSetup()
{
    m_accountManager = Tp::AccountManager::create(Tp::AccountFactory::create(QDBusConnection::sessionBus(),
                                                                            Tp::Account::FeatureCore),
                                                 Tp::ConnectionFactory::create(QDBusConnection::sessionBus(),
                                                                               Tp::Connection::FeatureCore));
    connect(m_accountManager->becomeReady(),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(accountManagerReady(Tp::PendingOperation*)));
}

void TelepathyMonitor::accountManagerReady(Tp::PendingOperation* operation)
{
    if (operation->isError()) {
        qDebug() << "TelepathyMonitor: accountManager init error.";
        QTimer::singleShot(1000, this, &TelepathyMonitor::accountManagerSetup); // again
        return;
    }
    qDebug() << "Telepathy account manager ready";

    foreach (const Tp::AccountPtr& account, m_accountManager->allAccounts()) {
        connect(account->becomeReady(Tp::Account::FeatureCapabilities),
                SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(accountReady(Tp::PendingOperation*)));
    }

    connect(m_accountManager.data(), SIGNAL(newAccount(Tp::AccountPtr)), SLOT(newAccount(Tp::AccountPtr)));
}

void TelepathyMonitor::newAccount(const Tp::AccountPtr& account)
{
    connect(account->becomeReady(Tp::Account::FeatureCapabilities),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(accountReady(Tp::PendingOperation*)));
}

void TelepathyMonitor::accountReady(Tp::PendingOperation* operation)
{
    if (operation->isError()) {
        qDebug() << "TelepathyAccount: Operation failed (accountReady)";
        return;
    }

    Tp::PendingReady* pendingReady = qobject_cast<Tp::PendingReady*>(operation);
    if (pendingReady == 0) {
        qDebug() << "Rejecting account because could not understand ready status";
        return;
    }
    checkAndAddAccount(Tp::AccountPtr::qObjectCast(pendingReady->proxy()));
}

void TelepathyMonitor::onCallStarted(Tp::CallChannelPtr callChannel)
{
    m_cookie++;
    m_currentCalls.insert(m_cookie, callChannel.data());
    m_currentCallStates.insert(m_cookie, Tp::CallStateInitialising);

    callChannel->becomeReady(Tp::CallChannel::FeatureCallState);

    connect(callChannel.data(), &Tp::CallChannel::callStateChanged, this, &TelepathyMonitor::callStateChanged);

    QString number = callChannel->initiatorContact()->id();

    // try to match the contact info
    QContactFetchRequest *request = new QContactFetchRequest(this);
    request->setFilter(QContactPhoneNumber::match(number));

    // lambda function to update the notification
    QObject::connect(request, &QContactAbstractRequest::stateChanged, [this, request, number](QContactAbstractRequest::State state) {
        qDebug() << "request returned";
        if (!request || state != QContactAbstractRequest::FinishedState) {
            qDebug() << "error fetching contact" << state;
            return;
        }

        QContact contact;

        // create the snap decision only after the contact match finishes
        if (request->contacts().size() > 0) {
            // use the first match
            contact = request->contacts().at(0);

            qDebug() << "have contact" << contact.detail<QContactDisplayLabel>().label();
            emit this->incomingCall(m_cookie, number, contact.detail<QContactDisplayLabel>().label());
        } else {
            qDebug() << "unknown contact" << number;
            emit this->incomingCall(m_cookie, number, QString());
        }
    });

    request->setManager(m_contactManager);
    request->start();
}

void TelepathyMonitor::callStateChanged(Tp::CallState state)
{
    qDebug() << "call state changed1";
    Tp::CallChannel *channel = qobject_cast<Tp::CallChannel*>(sender());
    uint cookie = m_currentCalls.key(channel);

    qDebug() << "call state changed2" << state << "cookie:" << cookie;

    switch (state) {
    case Tp::CallStateActive:
        emit callStarted(cookie);
        m_currentCallStates[cookie] = Tp::CallStateActive;
        break;
    case Tp::CallStateEnded: {
        Tp::CallState oldState = m_currentCallStates.value(cookie);
        emit callEnded(cookie, oldState != Tp::CallStateActive);
        m_currentCalls.take(cookie);
        m_currentCallStates.take(cookie);
        break;
    }
    default:
        break;
    }
}

void TelepathyMonitor::checkAndAddAccount(const Tp::AccountPtr& account)
{
    Tp::ConnectionCapabilities caps = account->capabilities();
    // TODO: Later on we will need to filter for the right capabilities, and also allow dynamic account detection
    // Don't check caps for now as a workaround for https://bugs.launchpad.net/ubuntu/+source/media-hub/+bug/1409125
    // at least until we are able to find out the root cause of it (check rev 107 for the caps check)
    auto tcm = new TelepathyCallMonitor(account);
    connect(tcm, &TelepathyCallMonitor::callStarted, this, &TelepathyMonitor::onCallStarted);
    m_callMonitors.append(tcm);
}
