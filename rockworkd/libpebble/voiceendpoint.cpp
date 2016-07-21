#include "voiceendpoint.h"

#include "pebble.h"
#include "watchdatawriter.h"
#include "watchdatareader.h"
#include "watchconnection.h"

#include <QTimerEvent>
#include <QDebug>

VoiceEndpoint::VoiceEndpoint(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_watchConnection(connection)
{
    qDebug() << "Attaching endpoint to bus" << WatchConnection::EndpointVoiceControl << WatchConnection::EndpointAudioStream;
    m_watchConnection->registerEndpointHandler(WatchConnection::EndpointVoiceControl, this, "handleMessage");
    m_watchConnection->registerEndpointHandler(WatchConnection::EndpointAudioStream, this, "handleFrame");
}

/*
 * The example session from Pebble SDK (pebble transcribe --phone jolla -vvvv "pebble sdk test 4")
DEBUG:libpebble2.communication:<- 00292af8010000000001120001011d00312e327263310000000000000000000000000000803e00000032044001
DEBUG:libpebble2.communication:<- VoiceControlCommand(command=Command.SessionSetup, flags=0, data=SessionSetupCommand(session_type=SessionType.Dictation, session_id=18, attributes=AttributeList(count=1, dictionary=[Attribute(id=AttributeType.SpeexEncoderInfo, length=29, data=SpeexEncoderInfo(version=1.2rc1, sample_rate=16000, bit_rate=12800, bitstream_version=4, frame_size=320))])))
DEBUG:libpebble2.voice:
DEBUG:libpebble2.voice:)
DEBUG:libpebble2.communication:-> VoiceControlResult(command=None, flags=0, data=SessionSetupResult(session_type=SessionType.Dictation, result=SetupResult.Success))
DEBUG:libpebble2.communication:-> 00072af801000000000100
DEBUG:libpebble2.communication:<- 00252710021200012025ce7530001b366cd9b1c09b364c62d2233fe25a16468763d58d6f493a1051f3
DEBUG:libpebble2.communication:<- AudioStream(packet_id=2, session_id=18, data=DataTransfer(frame_count=1, frames=[EncoderFrame(data=25ce7530001b366cd9b1c09b364c62d223...)]))
DEBUG:libpebble2.communication:<- 00252710021200012025458925e3ac89339017d185d97e754b91057ceccbb02340e485f2c93867ba72
DEBUG:libpebble2.communication:<- AudioStream(packet_id=2, session_id=18, data=DataTransfer(frame_count=1, frames=[EncoderFrame(data=25458925e3ac89339017d185d97e754b91...)]))
DEBUG:libpebble2.communication:<- 00252710021200012025ce45157c0ce4fbd80e69f812582aa36945040a247e292755e81139396a3a71
DEBUG:libpebble2.communication:<- AudioStream(packet_id=2, session_id=18, data=DataTransfer(frame_count=1, frames=[EncoderFrame(data=25ce45157c0ce4fbd80e69f812582aa369...)]))
DEBUG:libpebble2.communication:<- 00032710031200
DEBUG:libpebble2.communication:<- AudioStream(packet_id=3, session_id=18, data=StopTransfer())
DEBUG:libpebble2.communication:-> AudioStream(packet_id=None, session_id=18, data=StopTransfer())
DEBUG:libpebble2.communication:-> 00032710031200
DEBUG:libpebble2.voice:)
DEBUG:libpebble2.communication:-> VoiceControlResult(command=None, flags=0, data=DictationResult(session_id=18, result=TranscriptionResult.Success, attributes=AttributeList(count=None, dictionary=[Attribute(id=AttributeType.Transcription, length=None, data=Transcription(type=None, transcription=SentenceList(count=None, sentences=[Sentence(count=None, words=[Word(confidence=100, length=None, data=706562626c65), Word(confidence=100, length=None, data=73646b), Word(confidence=100, length=None, data=74657374), Word(confidence=100, length=None, data=34)])])))])))
DEBUG:libpebble2.communication:-> 002a2af8020000000012000001021e0001010400640600706562626c6564030073646b6404007465737464010034
DEBUG:libpebble2.voice:Invalidating session 18 on send
DEBUG:libpebble2.voice:Invalidating session 0 on stop
 * serialized raw framed packet of result response
 * 002a 2af8 02 00000000 1200 00 01 02 1e00 01 01 0400 64 0600 706562626c65 64 0300 73646b 64 0400 74657374 64 0100 34
 * 002a Framing - length
 * 2af8 Framing - endpoint
 * 02 - DictationResult command
 * 00000000 - flags
 * 1200 - sessionId (LE)
 * 00 - Success (result)
 * 01 - attributes:count
 * 02 - attribute:type - Transcription
 * 1e00 - attribute:length (LE)
 * 01 - Transcription Type - SentenceList
 * 01 - SentenceList:count
 * 0400 - Sentence:count (LE)
 * 64 - confidence
 * 0600 - length
 * 706562626c65 - data
 * ...
[D] VoiceEndpoint::sendDictationResults:207 - Sending session recognition result 0 for session 27 with content 1 for app QUuid("{00000000-0000-0000-0000-000000000000}") true
[D] VoiceEndpoint::AttributeList::writerWrite:305 - Writing attribute "Attribute(type=2, Transcription(type=SentenceList, SentenceList(count=1, sentences=[Sentence(count=1, words=[Word(confidence=100, word=Timeout)])])))"
[D] VoiceEndpoint::Attribute::writerWrite:352 - Serializing Transcription 0 1
[D] VoiceEndpoint::sendDictationResults:222 - Sent "0200000000001b0001020f000101010064080054696d656f757400"
 * Serialized unframed voice control packet
 * 02 00000000 0300 00 01 02 0f00 01 01 0100 64 0800 54696d656f757400
 * 02   - DictationResult
 * 0300 - sessionId
 * 00   - Success (result)
 * 01   - AttributeList:count
 * 02   - Attibute:Type - Transcription
 * 0f00 - Attribute:Length
 * 01   - Transcription:Type - SentenceList
 * 01   - SentenceList:Count
 * 01   - Sentence:Count
 * 64   - Word:Confidence
 * 0800 - Word:Length
 * 54696d656f757400 - data (Timeout\0)
 */
void VoiceEndpoint::handleMessage(const QByteArray &data)
{
    WatchDataReader reader(data);
    quint8 cmd = reader.read<quint8>();

    if(cmd == CmdSessionSetup) {
        quint32 flags = reader.readLE<quint32>();
        quint8 sessType = reader.read<quint8>();
        quint16 sesId = reader.readLE<quint16>();
        if(sesId > 0 && m_sessId == 0) {
            AttributeList list = readAttributes(reader);
            if(((flags & FlagAppInitiated)!=0) == list.contains(AttAppUuid)) {
                if(sessType == SesDictation) {
                    qDebug() << "Got Dictation session request" << sesId;
                    if(list.count > 0 && list.contains(AttSpeexInfo)) {
                        m_sessId = sesId;
                        m_sesType = (SessionType)sessType;
                        m_appUuid = list.attByType(AttAppUuid).uuid;
                        m_codec = list.attByType(AttSpeexInfo).speexInfo;
                        qDebug() << "Session Setup Request" << ((flags&FlagAppInitiated)?list.attByType(AttAppUuid).uuid.toString():"");
                        emit sessionSetupRequest(m_appUuid,m_codec);
                        m_sesPhase = PhSetupRequest;
                        m_sesTimer = startTimer(4000);
                    } else {
                        qWarning() << "Invalid attribute set for dictation request" << list.count;
                    }
                } else if(sessType == SesCommand) {
                    qDebug() << "Got Voice Command session request which is rather unexpected";
                } else {
                    qWarning() << "Unknown Session type" << sessType << data.toHex();
                }
            } else {
                qWarning() << "Invalid Flags/Attributes combination" << flags;
            }
        } else {
            qWarning() << "Invalid sessionID for session setup" << sesId << m_sessId << data.toHex();
        }
    } else {
        qWarning() << "Unknown command" << data.toHex();
    }
}

void VoiceEndpoint::sessionSetupResponse(Result result, const QUuid &appUuid)
{
    if(m_sessId>0) {
        qDebug() << "Sending session setup result" << result << "at session" << m_sessId << "of type" << m_sesType << "with app" << appUuid;
        quint32 flags = appUuid.isNull()?0:FlagAppInitiated;
        QByteArray pkt;
        WatchDataWriter writer(&pkt);
        pkt.append((quint8)CmdSessionSetup);
        writer.writeLE<quint32>(flags);
        pkt.append((quint8)m_sesType);
        pkt.append((quint8)result);
        m_watchConnection->writeToPebble(WatchConnection::EndpointVoiceControl,pkt);
        m_sesPhase = PhSetupComplete;
        if(result != ResSuccess) {
            sessionDestroy();
        } else {
            if(m_sesTimer)
                killTimer(m_sesTimer);
            m_sesTimer = startTimer(6000);
        }
    }
}
/**
 * @brief VoiceEndpoint::handleFrame
 * @param data
 * Handles raw unframed packet for audio endpoint
 * It handles both - bitstream and stop packet
 * When bitstream stops it fires a timer to detect timeout and cleanup session
 * Important: response should be made within 2000ms window after stop
 */
void VoiceEndpoint::handleFrame(const QByteArray &data)
{
    WatchDataReader reader(data);
    quint8 cmd = reader.read<quint8>();
    quint16 sid = reader.readLE<quint16>();

    if(cmd == FrmDataTransfer) {
        if(sid == m_sessId) {
            AudioStream str;
            str.count = reader.read<quint8>();
            for(int i=0;i<str.count;i++) {
                Frame frm;
                frm.length = reader.read<quint8>();
                frm.data = reader.readBytes(frm.length);
                str.frames.append(frm);
            }
            emit audioFrame(sid,str);
            m_sesPhase = PhAudioStarted;
        } else {
            stopAudioStream(sid);
        }
    } else if(cmd == FrmStopTransfer) {
        if(sid != m_sessId)
            return;
        if(m_sesTimer)
            killTimer(m_sesTimer);
        qDebug() << "Pebble finished sending audio at session" << m_sessId;
        emit audioFrame(m_sessId,AudioStream());
        m_sesPhase = PhAudioStopped;
        m_sesTimer = startTimer(1500);
    } else {
        qWarning() << "Unknown audio frame type" << data.toHex();
    }
}

void VoiceEndpoint::stopAudioStream()
{
    stopAudioStream(m_sessId);
}
void VoiceEndpoint::stopAudioStream(quint16 sid)
{
    if(sid>0) {
        qDebug() << "Terminating audio stream for session" << sid;
        QByteArray pkt;
        WatchDataWriter writer(&pkt);
        writer.write<quint8>(FrmStopTransfer);
        writer.writeLE<quint16>(sid);
        m_watchConnection->writeToPebble(WatchConnection::EndpointAudioStream, pkt);
    }
}
void VoiceEndpoint::transcriptionResponse(Result result, const QList<Sentence> &data, const QUuid &appUuid)
{
    if(m_sessId>0) {
        qDebug() << "Results submitted with status" << result << "data" << data.count() << "for" << appUuid.toString();
        if(result == ResSuccess) {
            if(appUuid.isNull() || appUuid == m_appUuid) {
                if(data.count()>0) {
                    m_sesResult.append(data);
                    if(m_sesPhase==PhAudioStopped) {
                        if(m_sesTimer)
                            killTimer(m_sesTimer);
                        m_sesPhase = PhResultReceived;
                        m_sesTimer = startTimer(500);
                    }
                }
            }
        }
    }
}
/**
 * @brief VoiceEndpoint::sendDictationResults
 * Important: Pebble accepts only 1 sentence currently, hence need to choose the best one here
 */
void VoiceEndpoint::sendDictationResults()
{
    if(m_sessId>0) {
        if(m_sesTimer) {
            killTimer(m_sesTimer);
            m_sesTimer = 0;
            m_sesPhase = PhResultSent;
        }
        quint32 flags = m_appUuid.isNull()?0:FlagAppInitiated;
        quint8 result = (m_sesResult.sentences.count()>0)?ResSuccess:ResInvalidRecognizerResponse;
        qDebug() << "Sending session recognition result" << result << "for session" << m_sessId << "with content" << m_sesResult.sentences.count() << "for app" << m_appUuid << m_appUuid.isNull();
        QByteArray pkt;
        WatchDataWriter writer(&pkt);
        writer.write<quint8>(CmdDictatResult);
        writer.writeLE<quint32>(flags);
        writer.writeLE<quint16>(m_sessId);
        writer.write<quint8>(result);
        AttributeList al;
        if(!m_appUuid.isNull())
            al.append(Attribute(m_appUuid));
        m_sesResult.sort(1);
        if(result==ResSuccess)
            al.append(Attribute(Transcription(m_sesResult)));
        al.writerWrite(writer);
        m_watchConnection->writeToPebble(WatchConnection::EndpointVoiceControl,pkt);
        qDebug() << "Sent" << pkt.toHex();
    }
}
void VoiceEndpoint::sessionDestroy()
{
    if(m_sesTimer) {
        killTimer(m_sesTimer);
        m_sesTimer = 0;
    }
    m_sesResult.sentences.clear();
    m_appUuid = QUuid();
    m_sesPhase = PhSessionClosed;
    emit sessionCloseNotice(m_sessId);
    m_sessId = 0;
    qDebug() << "Session closed, state reset to initial";
}

void VoiceEndpoint::timerEvent(QTimerEvent *event)
{
    if(m_sesTimer > 0 && event && event->timerId() == m_sesTimer) {
        killTimer(m_sesTimer);
        m_sesTimer = 0;
        switch (m_sesPhase) {
        case PhSetupRequest:
            sessionSetupResponse(ResTimeout,QUuid());
            break;
        case PhSetupComplete:
            sessionSetupResponse(ResInvalidMessage,QUuid());
            break;
        case PhAudioStarted:
            stopAudioStream(m_sessId);
            break;
        case PhAudioStopped:
            //transcriptionResponse(ResTimeout,QList<Sentence>(),QUuid()); // We should actualy reply like this but lets do...
            transcriptionResponse(ResSuccess,
                            QList<Sentence>({
                                Sentence({8,QList<Word>({{1,2,"No"},{1,3,"one"},{1,5,"dared"},{1,2,"to"},{1,5,"reply"},{1,1,"."},{1,7,"Service"},{1,7,"Timeout"}})})
                            }),
                            m_appUuid); // Example transcription - for the reference
            break;
        case PhResultReceived:
            sendDictationResults();
            sessionDestroy();
            break;
        default:
            qDebug() << "Unhandled timer event for phase" << m_sesPhase;
        }
    }
}

/**
 * @brief VoiceEndpoint::AttributeList
 * @return
 */
VoiceEndpoint::AttributeList::AttributeList():
    count(0)
{
    //
}
void VoiceEndpoint::AttributeList::append(const Attribute &att)
{
    m_aidx.insert(att.type,(quint8)m_atts.size());
    m_atts.append(att);
}
const VoiceEndpoint::Attribute& VoiceEndpoint::AttributeList::attByNum(quint8 num) const
{
    return m_atts.at(num);
}
bool VoiceEndpoint::AttributeList::contains(AttributeType type) const
{
    return m_aidx.contains(type);
}
static const VoiceEndpoint::Attribute invalidAttr;
const VoiceEndpoint::Attribute& VoiceEndpoint::AttributeList::attByType(AttributeType type) const
{
    if(m_aidx.contains(type))
        return m_atts.at(m_aidx.value(type));
    return invalidAttr;
}
void VoiceEndpoint::AttributeList::writerWrite(WatchDataWriter &writer)
{
    count = m_atts.count();
    writer.write<quint8>(count);
    for(int i=0;i<count;i++) {
        qDebug() << "Writing attribute" << m_atts.at(i).toString();
        m_atts[i].writerWrite(writer);
    }
}

VoiceEndpoint::Attribute::Attribute(const Transcription &tr):
    type(VoiceEndpoint::AttTranscription),
    length(0),
    transcription(tr)
{
}
VoiceEndpoint::Attribute::Attribute(const QUuid &appUuid):
    type(AttAppUuid),
    length(16),
    uuid(appUuid)
{
}
VoiceEndpoint::Attribute::Attribute(const SpeexInfo &codec):
    type(AttSpeexInfo),
    length(29),
    speexInfo(codec)
{
}
VoiceEndpoint::Attribute::Attribute(quint8 type, quint16 length, const QByteArray &data):
    type(type),
    length(length),
    buf(data)
{
}

QByteArray& VoiceEndpoint::Attribute::serialize(QByteArray &in)
{
    if(buf.length()>0)
        in.append(buf);
    else {
        WatchDataWriter writer(&in);
        writerWrite(writer);
    }
    return in;
}
void VoiceEndpoint::Attribute::writerWrite(WatchDataWriter &writer)
{
    writer.write<quint8>(type);
    if(buf.length()>0) {
        writer.writeLE<quint16>(buf.length());
        writer.writeBytes(buf.length(),buf);
    } else if(type == AttTranscription) {
        qDebug() << "Serializing Transcription" << length << transcription.type;
        if(length) {
            writer.writeLE<quint16>(length);
            transcription.writerWrite(writer);
        } else {
            QByteArray _buf;
            _buf = transcription.serialize(_buf);
            length = _buf.length();
            writer.writeLE<quint16>(length);
            writer.writeBytes(length,_buf);
        }
    } else if(type == AttAppUuid) {
        writer.writeLE<quint16>(16);
        writer.writeUuid(uuid);
    } else if(type == AttSpeexInfo) {
        writer.writeLE<quint16>(29);
        writer.writeBytes(20,speexInfo.version);
        writer.writeLE<quint32>(speexInfo.sampleRate);
        writer.writeLE<quint16>(speexInfo.bitRate);
        writer.write<quint8>(speexInfo.bitstreamVer);
        writer.writeLE<quint16>(speexInfo.frameSize);
    } else {
        qWarning() << "Serialisation impossible for zero type" << type;
    }
}
QString VoiceEndpoint::Attribute::toString() const
{
    return QString("Attribute(type=%1, %2)").arg(QString::number(type),(
                (type==AttSpeexInfo)?QString("SpeexInfo()"):
                                     (type==AttAppUuid)?uuid.toString():
                                                        (type==AttTranscription)?QString("Transcription(%1)").arg(transcription.toString()):
                                                                                 QString("UnknownAttribute(QByteArray(%1))").arg(QString(buf))
                ));
}

QByteArray& VoiceEndpoint::Transcription::serialize(QByteArray &buf) const
{
    WatchDataWriter writer(&buf);
    writerWrite(writer);
    return buf;
}
void VoiceEndpoint::Transcription::writerWrite(WatchDataWriter &writer) const
{
    writer.write<quint8>(type);
    sentenceList.writerWrite(writer);
}
QString VoiceEndpoint::Transcription::toString() const
{
    return QString("type=%1, %2").arg((type==TrSentenceList)?"SentenceList":QString::number(type)).arg(sentenceList.toString());
}

QByteArray& VoiceEndpoint::SentenceList::serialize(QByteArray &buf) const
{
    WatchDataWriter writer(&buf);
    writerWrite(writer);
    return buf;
}
void VoiceEndpoint::SentenceList::writerWrite(WatchDataWriter &writer) const
{
    writer.write<quint8>(count);
    for(int i=0;i<count;i++) {
        Sentence st = sentences.at(i);
        writer.writeLE<quint16>(st.count);
        if(st.count>st.words.count()) st.count = st.words.count();
        for(int j=0;j<st.count;j++) {
            Word word = st.words.at(j);
            writer.write<quint8>(word.confidence);
            writer.writeLE<quint16>(word.length);
            writer.writeBytes(word.length,word.data);
        }
    }
}
void VoiceEndpoint::SentenceList::sort(int trim)
{
    if(trim>0 && sentences.count()==1) {
        count = 1;
        return;
    }
    count = (quint8)qMin(255,qMin(trim,sentences.count()));
    QList<quint8> avg;
    for(int i=0;i<count;i++) {
        int m=i;
        int sum=0;
        for(int k=0;k<sentences.at(i).words.count();k++){
            sum+=sentences.at(i).words.at(k).confidence;
        }
        avg.append(sum / sentences.at(m).words.count());
        for(int j=i+1;j<sentences.count();j++) {
            if(avg.count()<=j) {
                sum=0;
                for(int k=0;k<sentences.at(j).words.count();k++){
                    sum+=sentences.at(j).words.at(k).confidence;
                }
                avg.append(sum / sentences.at(j).words.count());
            }
            if(avg.at(m) < avg.at(j))
                m=j;
        }
        if(m!=i) {
            Sentence s=sentences.at(i);
            sentences[i]=sentences[m];
            sentences[m]=s;
            avg[m]=avg[i];
        }
    }
}
QString VoiceEndpoint::SentenceList::toString() const
{
    QString sl;
    foreach(const Sentence &s,sentences) {
        QString wl;
        foreach(const Word &w,s.words) {
            wl += QString("Word(confidence=%1, word=%2), ").arg(QString::number(w.confidence),QString::fromUtf8(w.data));
        }
        sl += QString("Sentence(count=%1, words=[%2]), ").arg(QString::number(s.count),wl);
    }
    return QString("SentenceList(count=%1, sentences=[%2])").arg(QString::number(count),sl);
}

VoiceEndpoint::AttributeList VoiceEndpoint::readAttributes(WatchDataReader &reader)
{
    AttributeList list;
    list.count = reader.read<quint8>();
    for(int i=0;i<list.count;i++) {
        Attribute att;
        att.type = reader.read<quint8>();
        att.length = reader.readLE<quint16>();
        switch(att.type) {
        case AttSpeexInfo:
            att.speexInfo.version = reader.readBytes(20);
            att.speexInfo.sampleRate = reader.readLE<quint32>();
            att.speexInfo.bitRate = reader.readLE<quint16>();
            att.speexInfo.bitstreamVer = reader.read<quint8>();
            att.speexInfo.frameSize = reader.readLE<quint16>();
            break;
        case AttAppUuid:
            att.uuid = reader.readUuid();
            break;
        case AttTranscription:
            att.transcription.type = reader.read<quint8>();
            att.transcription.sentenceList.count = reader.read<quint8>();
            for(int j=0;j<att.transcription.sentenceList.count;j++) {
                Sentence st;
                st.count = reader.readLE<quint16>();
                for(int k=0;k<st.count;k++) {
                    Word word;
                    word.confidence = reader.read<quint8>();
                    word.length = reader.readLE<quint16>();
                    word.data = reader.readBytes(word.length);
                    st.words.append(word);
                }
                att.transcription.sentenceList.sentences.append(st);
            }
            break;
        default:
            att.buf = reader.readBytes(att.length);
            qWarning() << "Unknown attribute type" << att.type << att.buf.toHex();
        }
        list.append(att);
    }
    return list;
}
