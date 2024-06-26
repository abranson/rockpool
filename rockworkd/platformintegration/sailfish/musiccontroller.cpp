/*
 *  libwatchfish - library with common functionality for SailfishOS smartwatch connector programs.
 *  Copyright (C) 2015 Javier S. Pedro <dev.git@javispedro.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QtCore/QDir>
#include <QtCore/QCryptographicHash>
#include <QDBusMessage>
#include <QDBusReply>
#include <AmberMpris/mprismetadata.h>
#include "musiccontroller.h"

namespace watchfish
{

Q_LOGGING_CATEGORY(musicControllerCat, "watchfish-MusicController")

MusicControllerPrivate::MusicControllerPrivate(MusicController *q)
	: controller(new Amber::MprisController(this)), q_ptr(q)
{
	connect(controller, &Amber::MprisController::currentServiceChanged,
			this, &MusicControllerPrivate::handleCurrentServiceChanged);
	connect(controller, &Amber::MprisController::playbackStatusChanged,
			this, &MusicControllerPrivate::handlePlaybackStatusChanged);
    connect(controller, &Amber::MprisController::seeked,
            this, &MusicControllerPrivate::handlePositionChanged);
	connect(controller->metaData(), &Amber::MprisMetaData::metaDataChanged,
			this, &MusicControllerPrivate::handleMetadataChanged);
	connect(controller, &Amber::MprisController::shuffleChanged,
			q, &MusicController::shuffleChanged);
	connect(controller, &Amber::MprisController::loopStatusChanged,
			q, &MusicController::repeatChanged);
}

MusicControllerPrivate::~MusicControllerPrivate()
{
}

void MusicControllerPrivate::updateStatus()
{
	Q_Q(MusicController);
	QString service = controller->currentService();
	MusicController::Status newStatus;

	if (service.isEmpty()) {
		newStatus =  MusicController::StatusNoPlayer;
	} else {
		switch (controller->playbackStatus()) {
		case Amber::Mpris::Playing:
			newStatus = MusicController::StatusPlaying;
			break;
		case Amber::Mpris::Paused:
			newStatus = MusicController::StatusPaused;
			break;
		default:
			newStatus = MusicController::StatusStopped;
			break;
		}
	}

	if (newStatus != curStatus) {
		curStatus = newStatus;
		emit q->statusChanged();
	}
}

void MusicControllerPrivate::updateMetadata()
{
	Q_Q(MusicController);

	QString newArtist = controller->metaData()->albumArtist().toString(),
			newAlbum = controller->metaData()->albumTitle().toString(),
			newTitle = controller->metaData()->title().toString();

	if (newArtist != curArtist) {
		curArtist = newArtist;
		emit q->artistChanged();
	}

	if (newAlbum != curAlbum) {
		curAlbum = newAlbum;
		emit q->albumChanged();
	}

	if (newTitle != curTitle) {
		curTitle = newTitle;
		emit q->titleChanged();
	}

	int newDuration = controller->metaData()->duration().toULongLong();
	if (newDuration != curDuration) {
		curDuration = newDuration;
		emit q->durationChanged();
	}

	emit q->metadataChanged();
}

void MusicControllerPrivate::handleCurrentServiceChanged()
{
	Q_Q(MusicController);
	qCDebug(musicControllerCat()) << controller->currentService();
	updateStatus();
	emit q->serviceChanged();
}

void MusicControllerPrivate::handlePlaybackStatusChanged()
{
	qCDebug(musicControllerCat()) << controller->playbackStatus();
	updateStatus();
}

void MusicControllerPrivate::handlePositionChanged(qlonglong position)
{
    Q_Q(MusicController);
    qCDebug(musicControllerCat()) << "Position changed: " << position;
    emit q->positionChanged();
}

void MusicControllerPrivate::handleMetadataChanged()
{
	updateMetadata();
}

MusicController::MusicController(QObject *parent)
    : QObject(parent), d_ptr(new MusicControllerPrivate(this)),
      _pulseBus(NULL), _maxVolume(0)
{
    connectPulseBus();
}

MusicController::~MusicController()
{
    if (_pulseBus != NULL) {
        qDebug() << "Disconnecting from PulseAudio P2P DBus";
        QDBusConnection::disconnectFromBus("org.PulseAudio1");
        delete(_pulseBus);
    }
    delete d_ptr;
}

void MusicController::connectPulseBus() {
    if (_pulseBus) {
        if (!_pulseBus->isConnected())
            delete(_pulseBus);
        else
            return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall("org.PulseAudio1", "/org/pulseaudio/server_lookup1", "org.freedesktop.DBus.Properties", "Get" );
    call << "org.PulseAudio.ServerLookup1" << "Address";
    QDBusReply<QDBusVariant> lookupReply = QDBusConnection::sessionBus().call(call);
    if (lookupReply.isValid()) {
        qDebug() << "PulseAudio Bus address: " << lookupReply.value().variant().toString();
        _pulseBus = new QDBusConnection(QDBusConnection::connectToPeer(lookupReply.value().variant().toString(), "org.PulseAudio1"));
        if (_maxVolume == 0) {
            // Query max volume
            call = QDBusMessage::createMethodCall("com.Meego.MainVolume2", "/com/meego/mainvolume2",
                                                               "org.freedesktop.DBus.Properties", "Get");
            call << "com.Meego.MainVolume2" << "StepCount";
            QDBusReply<QDBusVariant> volumeMaxReply = _pulseBus->call(call);
            if (volumeMaxReply.isValid()) {
                _maxVolume = volumeMaxReply.value().variant().toUInt();
                qDebug() << "Max volume: " << _maxVolume;
            }
            else {
                qWarning() << "Could not read volume max, cannot adjust volume: " << volumeMaxReply.error().message();
            }
        }
    }
    else
        qDebug() << "Cannot connect to PulseAudio bus";
}

MusicController::Status MusicController::status() const
{
	Q_D(const MusicController);
	return d->curStatus;
}

QString MusicController::service() const
{
	Q_D(const MusicController);
	return d->controller->currentService();
}

QString MusicController::title() const
{
	Q_D(const MusicController);
	return d->curTitle;
}

QString MusicController::album() const
{
	Q_D(const MusicController);
	return d->curAlbum;
}

QString MusicController::artist() const
{
	Q_D(const MusicController);
	return d->curArtist;
}

int MusicController::duration() const
{
	Q_D(const MusicController);
	return d->curDuration;
}

qlonglong MusicController::position() const
{
    Q_D(const MusicController);
    return d->controller->position();
}

MusicController::RepeatStatus MusicController::repeat() const
{
	Q_D(const MusicController);
	switch (d->controller->loopStatus()) {
	case Amber::Mpris::LoopNone:
	default:
		return RepeatNone;
	case Amber::Mpris::LoopTrack:
		return RepeatTrack;
	case Amber::Mpris::LoopPlaylist:
		return RepeatPlaylist;
	}
}

bool MusicController::shuffle() const
{
	Q_D(const MusicController);
	return d->controller->shuffle();
}

int MusicController::volume() const
{
    uint volume = -1;

    QDBusMessage call = QDBusMessage::createMethodCall("com.Meego.MainVolume2", "/com/meego/mainvolume2",
                                                   "org.freedesktop.DBus.Properties", "Get");
    call << "com.Meego.MainVolume2" << "CurrentStep";

    QDBusReply<QDBusVariant> volumeReply = _pulseBus->call(call);
    if (volumeReply.isValid()) {
        // Decide the new value for volume, taking limits into account
        volume = volumeReply.value().variant().toUInt();
    }
    return volume;
}

void MusicController::play()
{
	Q_D(MusicController);
	d->controller->play();
}

void MusicController::pause()
{
	Q_D(MusicController);
	d->controller->pause();
}

void MusicController::playPause()
{
	Q_D(MusicController);
	d->controller->playPause();
}

void MusicController::next()
{
	Q_D(MusicController);
	d->controller->next();
}

void MusicController::previous()
{
	Q_D(MusicController);
	d->controller->previous();
}

void MusicController::setVolume(const uint newVolume)
{
    qDebug() << "Setting volume: " << newVolume;
    connectPulseBus();
    QDBusMessage call = QDBusMessage::createMethodCall("com.Meego.MainVolume2", "/com/meego/mainvolume2",
                                          "org.freedesktop.DBus.Properties", "Set");
    call << "com.Meego.MainVolume2" << "CurrentStep" << QVariant::fromValue(QDBusVariant(newVolume));

    QDBusError err = _pulseBus->call(call);
    if (err.isValid()) {
        qWarning() << err.message();
    }
}

void MusicController::volumeUp()
{
    connectPulseBus();
    uint curVolume = this->volume();
    uint newVolume = curVolume + 1;
    if (newVolume >= _maxVolume) {
        qDebug() << "Cannot increase volume beyond maximum " << _maxVolume;
        return;
    }
    setVolume(newVolume);
}

void MusicController::volumeDown()
{
    connectPulseBus();
    uint curVolume = this->volume();
    if (curVolume == 0) {
        qDebug() << "Cannot decrease volume beyond 0";
        return;
    }
    uint newVolume = curVolume - 1;

    setVolume(newVolume);
}

}
