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

#ifndef WATCHFISH_NOTIFICATION_H
#define WATCHFISH_NOTIFICATION_H

#include <QtCore/QObject>
#include <QtCore/QDateTime>
#include <QtCore/QStringList>

namespace watchfish
{

class NotificationPrivate;

class Notification : public QObject
{
	Q_OBJECT
	Q_DECLARE_PRIVATE(Notification)

	/** Notification ID */
	Q_PROPERTY(uint id READ id CONSTANT)
	/** Name of sender program */
	Q_PROPERTY(QString sender READ sender WRITE setSender NOTIFY senderChanged)
	Q_PROPERTY(QString summary READ summary WRITE setSummary NOTIFY summaryChanged)
	Q_PROPERTY(QString body READ body WRITE setBody NOTIFY bodyChanged)
	Q_PROPERTY(QString owner READ owner WRITE setOwner NOTIFY ownerChanged)
	Q_PROPERTY(uint replacesId READ replacesId WRITE setReplacesId NOTIFY replacesIdChanged)
	Q_PROPERTY(QString originPackage READ originPackage WRITE setOriginPackage NOTIFY originPackageChanged)
	Q_PROPERTY(QDateTime timestamp READ timestamp WRITE setTimestamp NOTIFY timestampChanged)
	/** Icon file path */
	Q_PROPERTY(QString icon READ icon WRITE setIcon NOTIFY iconChanged)
	Q_PROPERTY(int urgency READ urgency WRITE setUrgency NOTIFY urgencyChanged)
	Q_PROPERTY(bool transient READ transient WRITE setTransient NOTIFY transientChanged)
	Q_PROPERTY(bool hidden READ hidden WRITE setHidden NOTIFY hiddenChanged)

	/* Nemo stuff */
	Q_PROPERTY(QString previewSummary READ previewSummary WRITE setPreviewSummary NOTIFY previewSummaryChanged)
	Q_PROPERTY(QString previewBody READ previewBody WRITE setPreviewBody NOTIFY previewBodyChanged)
    Q_PROPERTY(QString feedback READ feedback WRITE setFeedback NOTIFY feedbackChanged)

	Q_PROPERTY(QStringList actions READ actions NOTIFY actionsChanged)

	Q_ENUMS(CloseReason)

public:
	explicit Notification(uint id, QObject *parent = 0);
	~Notification();

	enum CloseReason {
		Expired = 1,
		DismissedByUser = 2,
		DismissedByProgram = 3,
		ClosedOther = 4,
		Replaced = 5
	};

	quint32 id() const;

	quint32 replacesId() const;
	void setReplacesId(quint32 id);

	QString sender() const;
	void setSender(const QString &sender);

	QString summary() const;
	void setSummary(const QString &summary);

	QString category() const;
	void setCategory(const QString &category);

	QString body() const;
	void setBody(const QString &body);

	QString owner() const;
	void setOwner(const QString &owner);

	QString originPackage() const;
	void setOriginPackage(const QString &originPackage);

	QDateTime timestamp() const;
	void setTimestamp(const QDateTime &dt);

	QString icon() const;
	void setIcon(const QString &icon);

	int urgency() const;
	void setUrgency(int urgency);

	bool transient() const;
	void setTransient(bool transient);

	bool hidden() const;
	void setHidden(bool hidden);

	QString previewSummary() const;
	void setPreviewSummary(const QString &summary);

	QString previewBody() const;
	void setPreviewBody(const QString &body);

    QString feedback() const;
    void setFeedback(const QString &feedback);

	QStringList actions() const;
	void addDBusAction(const QString &action, const QString &service, const QString &path, const QString &iface, const QString &method, const QStringList &args = QStringList());
	QVariantList actionArgs(const QString &action) const;

public slots:
	void invokeAction(const QString &action);
	void close();

signals:
	void replacesIdChanged();
	void senderChanged();
	void summaryChanged();
	void categoryChanged();
	void bodyChanged();
	void ownerChanged();
	void originPackageChanged();
	void timestampChanged();
	void iconChanged();
	void urgencyChanged();
	void transientChanged();
	void hiddenChanged();

	void previewSummaryChanged();
	void previewBodyChanged();
	void feedbackChanged();

	void actionsChanged();

private:
	NotificationPrivate * const d_ptr;
};

}

#endif // WATCHFISH_NOTIFICATION_H
