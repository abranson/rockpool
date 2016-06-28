#ifndef VOICEENDPOINT_H
#define VOICEENDPOINT_H

#include <QObject>
#include <QHash>
#include <QUuid>

QT_FORWARD_DECLARE_CLASS(Pebble)
QT_FORWARD_DECLARE_CLASS(WatchConnection)
QT_FORWARD_DECLARE_CLASS(WatchDataReader)
QT_FORWARD_DECLARE_CLASS(WatchDataWriter)

struct SpeexInfo {
    QByteArray version;
    quint32 sampleRate;
    quint16 bitRate;
    quint8 bitstreamVer;
    quint16 frameSize;
};
struct Frame {
    quint8 length;
    QByteArray data;
};
struct AudioStream {
    quint8 count;
    QList<Frame> frames;
};

class VoiceEndpoint : public QObject
{
    Q_OBJECT
public:
    explicit VoiceEndpoint(Pebble *pebble, WatchConnection *connection);

    enum CommandType {
        CmdSessionSetup = 0x1,
        CmdDictatResult
    };
    enum SessionType {
        SesDictation = 0x1,
        SesCommand
    };
    enum AttributeType {
        AttSpeexInfo = 0x1,
        AttTranscription,
        AttAppUuid
    };
    enum TranscriptionType {
        TrSentenceList = 0x1
    };
    enum Result {
        ResSuccess,
        ResServiceUnavail,
        ResTimeout,
        ResRecognizerError,
        ResInvalidRecognizerResponse,
        ResDisabled,
        ResInvalidMessage
    };
    enum Falgs {
        FlagAppInitiated = 0x1
    };
    enum FrameType {
        FrmDataTransfer = 0x2,
        FrmStopTransfer
    };

    struct Word {
        quint8 confidence;
        quint16 length;
        QByteArray data;
    };
    struct Sentence {
        quint16 count;
        QList<Word> words;
    };
    struct SentenceList {
        quint8 count;
        QList<Sentence> sentences;

        SentenceList() {}
        SentenceList(const QList<Sentence> &sl) {count=sl.count();sentences=sl;}
        void append(const Sentence &sentence) {sentences.append(sentence);count=sentences.count();}
        void append(const QList<Sentence> &_snts) {sentences.append(_snts);count=sentences.count();}
        QByteArray& serialize(QByteArray &buf) const;
        void writerWrite(WatchDataWriter &writer) const;
        void sort(int trim);
        QString toString() const;
    };

    struct Transcription {
        quint8 type;
        SentenceList sentenceList;

        Transcription() {}
        Transcription(const SentenceList &sl) {type = TrSentenceList;sentenceList=sl;}
        void writerWrite(WatchDataWriter &writer) const;
        QByteArray& serialize(QByteArray &buf) const;
        QString toString() const;
    };
    struct Attribute {
        quint8 type;
        quint16 length;
        // union {
        Transcription transcription;
        SpeexInfo speexInfo;
        QUuid uuid;
        QByteArray buf;
        // };

        Attribute() {type=0;length=0;}
        Attribute(const Transcription &tr);
        Attribute(const QUuid &appUuid);
        Attribute(const SpeexInfo &codec);
        Attribute(quint8 type, quint16 length, const QByteArray &data);
        void writerWrite(WatchDataWriter &writer);
        QByteArray& serialize(QByteArray &buf);
        QString toString() const;
    };

    class AttributeList {
    public:
        AttributeList();
        quint8 count;
        void append(const Attribute &att);
        bool contains(AttributeType type) const;
        const Attribute& attByNum(quint8 num) const;
        const Attribute& attByType(AttributeType type) const;
        void writerWrite(WatchDataWriter &writer);
    private:
        QList<Attribute> m_atts;
        QHash<quint8,quint8> m_aidx;
    };
    struct SessionSetupResult {
        quint8 sessType;
        quint8 result;
    };
    struct DictationResult {
        quint16 sessId;
        quint8 result;
        AttributeList atts;
    };
    struct VoiceControlResult {
        quint8 command;
        quint32 flags;
        union {
            SessionSetupResult sessResult;
            DictationResult dictResult;
        } data;
    };

signals:
    void sessionSetupRequest(const QUuid &app_uuid, const SpeexInfo &codec);
    void audioFrame(quint16 sesId, const AudioStream &frame);
    void sessionCloseNotice(quint16 sesId);

public slots:
    void handleMessage(const QByteArray &data);
    void handleFrame(const QByteArray &data);

protected:
    void timerEvent(QTimerEvent *event) override;

public slots:
    void sessionSetupResponse(Result result, const QUuid &appUuid);
    void transcriptionResponse(Result result, const QList<Sentence> &data, const QUuid &appUuid);
    void sendDictationResults();
    void stopAudioStream();
    void stopAudioStream(quint16 sid);
    void sessionDestroy();

private:
    static inline AttributeList readAttributes(WatchDataReader &reader);

    enum SessionPhase {
        PhSessionClosed,
        PhSetupRequest,
        PhSetupComplete,
        PhAudioStarted,
        PhAudioStopped,
        PhResultReceived,
        PhResultSent
    };
    quint8 m_sesPhase;
    int m_sesTimer = 0;

    quint16 m_sessId = 0;
    QUuid m_appUuid;
    SpeexInfo m_codec;
    SessionType m_sesType;
    SentenceList m_sesResult;

    Pebble* m_pebble;
    WatchConnection* m_watchConnection;
};

#endif // VOICEENDPOINT_H
