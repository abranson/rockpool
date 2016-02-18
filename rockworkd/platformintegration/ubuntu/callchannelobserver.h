#ifndef CALLCHANNELOBSERVER_H
#define CALLCHANNELOBSERVER_H

#include <TelepathyQt/AccountManager>
#include <TelepathyQt/SimpleCallObserver>
#include <TelepathyQt/PendingOperation>
#include <TelepathyQt/PendingReady>
#include <TelepathyQt/PendingAccount>
#include <TelepathyQt/CallChannel>

#include <QContactManager>

QTCONTACTS_USE_NAMESPACE

class TelepathyCallMonitor : public QObject
{
    Q_OBJECT
public:
    TelepathyCallMonitor(const Tp::AccountPtr& account):
        mAccount(account),
        mCallObserver(Tp::SimpleCallObserver::create(mAccount, Tp::SimpleCallObserver::CallDirectionIncoming)) {
        connect(mCallObserver.data(), SIGNAL(callStarted(Tp::CallChannelPtr)), SIGNAL(callStarted(Tp::CallChannelPtr)));
//        connect(mCallObserver.data(), SIGNAL(callEnded(Tp::CallChannelPtr,QString,QString)), SIGNAL(callEnded()));
//        connect(mCallObserver.data(), SIGNAL(streamedMediaCallStarted(Tp::StreamedMediaChannelPtr)), SIGNAL(offHook()));
//        connect(mCallObserver.data(), SIGNAL(streamedMediaCallEnded(Tp::StreamedMediaChannelPtr,QString,QString)), SIGNAL(onHook()));
    }

signals:
    void callStarted(Tp::CallChannelPtr callChannel);
//    void callEnded();

private:
    Tp::AccountPtr mAccount;
    Tp::SimpleCallObserverPtr mCallObserver;
};

class TelepathyMonitor: public QObject
{
    Q_OBJECT
public:
    TelepathyMonitor(QObject *parent = 0);

    void hangupCall(uint cookie);

private slots:
    void accountManagerSetup();
    void accountManagerReady(Tp::PendingOperation* operation);

    void newAccount(const Tp::AccountPtr& account);
    void accountReady(Tp::PendingOperation* operation);

    void onCallStarted(Tp::CallChannelPtr callChannel);
    void callStateChanged(Tp::CallState state);

signals:
    void incomingCall(uint cookie, const QString &number, const QString &name);
    void callStarted(uint cookie);
    void callEnded(uint cookie, bool missed);

private:
    void checkAndAddAccount(const Tp::AccountPtr& account);

private:
    Tp::AccountManagerPtr m_accountManager;
    QList<TelepathyCallMonitor*> m_callMonitors;
    QContactManager *m_contactManager;

    QHash<uint, Tp::CallChannel*> m_currentCalls;
    QHash<uint, Tp::CallState> m_currentCallStates;

    uint m_cookie = 0;
};

#endif // CALLCHANNELOBSERVER_H
