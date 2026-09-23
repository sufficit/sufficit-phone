/*
 * Copyright (c) 2026 Sufficit.
 *
 * Internal update system for Sufficit Phone: discovers new releases on the
 * GitHub releases feed, downloads the platform package with SHA-256
 * verification against the release SHA256SUMS asset, and installs it only on
 * explicit user request.
 *
 * State machine (mirrors sufficit-ai-genius AppUpdateService design):
 *   Idle -> Checking -> UpToDate | Available -> Downloading -> Ready -> (install)
 *
 * This file is part of sufficit-phone (GPL-3.0, see LICENSE.txt).
 */

#ifndef APP_UPDATER_H_
#define APP_UPDATER_H_

#include <QObject>
#include <QString>
#include <QUrl>

#include "tool/AbstractObject.hpp"

class FileDownloader;
class QNetworkAccessManager;

class AppUpdater : public QObject, public AbstractObject {
	Q_OBJECT;

public:
	enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Error };
	Q_ENUM(State)

	Q_PROPERTY(State state READ getState NOTIFY stateChanged)
	Q_PROPERTY(QString availableVersion READ getAvailableVersion NOTIFY updateInfoChanged)
	Q_PROPERTY(QString assetName READ getAssetName NOTIFY updateInfoChanged)
	Q_PROPERTY(qint64 assetSize READ getAssetSize NOTIFY updateInfoChanged)
	Q_PROPERTY(qint64 readBytes READ getReadBytes NOTIFY downloadProgressChanged)
	Q_PROPERTY(qint64 totalBytes READ getTotalBytes NOTIFY downloadProgressChanged)
	Q_PROPERTY(QString progressText READ getProgressText NOTIFY downloadProgressChanged)
	Q_PROPERTY(QString errorMessage READ getErrorMessage NOTIFY stateChanged)
	Q_PROPERTY(QString installMode READ getInstallMode CONSTANT)
	Q_PROPERTY(QString releasePageUrl READ getReleasePageUrl NOTIFY updateInfoChanged)
	Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged)
	Q_PROPERTY(bool downloading READ isDownloading NOTIFY stateChanged)
	Q_PROPERTY(bool canCheck READ canCheck NOTIFY stateChanged)
	Q_PROPERTY(bool canDownload READ canDownload NOTIFY stateChanged)
	Q_PROPERTY(bool canInstall READ canInstall NOTIFY stateChanged)

	explicit AppUpdater(QObject *parent = nullptr);
	~AppUpdater() override;

	// requestedByUser: flow started from UI; background flows stay silent on
	// errors and only signal so App can decide what to show.
	Q_INVOKABLE void checkForUpdate(bool requestedByUser = false);
	Q_INVOKABLE void downloadUpdate();
	Q_INVOKABLE void installUpdate();
	Q_INVOKABLE void cancelDownload();

	State getState() const {
		return mState;
	}
	QString getAvailableVersion() const {
		return mAvailableVersion;
	}
	QString getAssetName() const {
		return mAssetName;
	}
	qint64 getAssetSize() const {
		return mAssetSize;
	}
	qint64 getReadBytes() const {
		return mReadBytes;
	}
	qint64 getTotalBytes() const {
		return mTotalBytes;
	}
	QString getErrorMessage() const {
		return mErrorMessage;
	}
	bool isBusy() const {
		return mState == State::Checking || mState == State::Downloading;
	}
	bool isDownloading() const {
		return mState == State::Downloading;
	}
	bool canCheck() const {
		return mState == State::Idle || mState == State::UpToDate || mState == State::Available ||
		       mState == State::Error;
	}
	bool canDownload() const {
		return mState == State::Available;
	}
	bool canInstall() const {
		return mState == State::Ready;
	}
	QString getReleasePageUrl() const;
	// "nsis" (Windows), "dmg" (macOS), "appimage" (Linux AppImage), "manual"
	// (Linux package-managed install: open the release page).
	QString getInstallMode() const;
	QString getProgressText() const;

	static QString repo(); // "owner/name", overridable via SUFFICIT_UPDATE_REPO

signals:
	void stateChanged();
	void updateInfoChanged();
	void downloadProgressChanged();
	void updateAvailable(const QString &version, qint64 sizeBytes, bool requestedByUser);
	void readyToInstall(const QString &version, bool requestedByUser);
	void upToDate(bool requestedByUser);
	void manualInstallOpened(); // installer file/page opened; App shows instructions popup
	void installFinished();     // on-disk swap done (AppImage path): App should restart.
	void updateError(const QString &message, bool userInitiated);

private:
	void setState(State state);
	void fail(const QString &message);
	void handleReleasesJson(const QByteArray &payload);
	void fetchChecksumThenDownload();
	void startAssetDownload(const QString &expectedChecksum);
	bool installAppImage();

	State mState = State::Idle;
	QString mErrorMessage;
	QString mAvailableVersion;
	QString mAssetName;
	QString mInstallerPath;
	QUrl mAssetUrl;
	QUrl mSumsUrl;
	qint64 mAssetSize = 0;
	qint64 mReadBytes = 0;
	qint64 mTotalBytes = 0;
	bool mRequestedByUser = false;

	QNetworkAccessManager *mNetwork = nullptr;
	FileDownloader *mDownloader = nullptr;

	DECLARE_ABSTRACT_OBJECT
};

#endif // APP_UPDATER_H_
