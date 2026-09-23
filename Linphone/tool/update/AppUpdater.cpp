/*
 * Copyright (c) 2026 Sufficit.
 *
 * Internal update system for Sufficit Phone. See AppUpdater.hpp.
 * This file is part of sufficit-phone (GPL-3.0, see LICENSE.txt).
 */

#include "AppUpdater.hpp"

#include "tool/Constants.hpp"
#include "tool/Utils.hpp"
#include "tool/file/FileDownloader.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QVersionNumber>
#include <algorithm>

DEFINE_ABSTRACT_OBJECT(AppUpdater)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// "6.2.0-alpha.2" / "v6.2.0" -> numeric segments + prerelease suffix ("" = stable).
struct ParsedVersion {
	QVector<int> segments;
	QString pre;
	bool valid = false;
};

ParsedVersion parseVersion(QString tag) {
	if (tag.startsWith(QStringLiteral("v"))) tag.remove(0, 1);
	ParsedVersion out;
	const int dash = tag.indexOf(QLatin1Char('-'));
	if (dash >= 0) out.pre = tag.mid(dash + 1).toLower();
	const QString numeric = (dash >= 0 ? tag.left(dash) : tag);
	const QVersionNumber version = QVersionNumber::fromString(numeric);
	out.segments = version.segments();
	out.valid = !version.isNull();
	return out;
}

// alpha(0) < beta(1) < rc/stable-suffix(2) < stable(3).
int preRank(const QString &pre) {
	if (pre.isEmpty()) return 3;
	if (pre.startsWith(QStringLiteral("alpha"))) return 0;
	if (pre.startsWith(QStringLiteral("beta"))) return 1;
	return 2;
}

int suffixNumber(const QString &pre) {
	static const QRegularExpression re(QStringLiteral("\\d+"));
	const auto m = re.match(pre);
	return m.hasMatch() ? m.captured(0).toInt() : 0;
}

// Extract the version stamped into the release asset names, e.g.
// "linphone-sdk-android-6.2.0-alpha.185+fa9d3260c.aar" -> "6.2.0-alpha.185+fa9d3260c".
// bc_compute_full_version counts commits, so the version embedded in the binaries
// does NOT match the release tag ("6.2.0-alpha.2" tag -> "6.2.0-alpha.185+hash"
// binary): comparing tags against the running version would be wrong.
QString embeddedVersion(const QJsonArray &assets) {
	// SDK assets always carry the exact computed version; prefer them. Client
	// package names are GitHub-sanitized ("6.2.0.alpha.2") and only used as a
	// fallback, where the numeric part is still meaningful.
	static const QRegularExpression re(
	    QStringLiteral("(\\d+\\.\\d+\\.\\d+(?:[.-][A-Za-z]+\\.[0-9]+)?(?:\\+[0-9a-fA-F]+)?)"));
	for (const auto &assetValue : assets) {
		const QString name = assetValue.toObject().value(QStringLiteral("name")).toString();
		if (name.startsWith(QStringLiteral("linphone-sdk-"))) {
			const auto m = re.match(name);
			if (m.hasMatch()) return m.captured(1);
		}
	}
	for (const auto &assetValue : assets) {
		const auto m = re.match(assetValue.toObject().value(QStringLiteral("name")).toString());
		if (m.hasMatch()) return m.captured(1);
	}
	return QString();
}

bool isNewerThan(const ParsedVersion &remote, const ParsedVersion &current) {
	if (!remote.valid || !current.valid) return false;
	const int segmentCount = qMax(remote.segments.size(), current.segments.size());
	for (int i = 0; i < segmentCount; ++i) {
		const int remotePart = i < remote.segments.size() ? remote.segments.at(i) : 0;
		const int currentPart = i < current.segments.size() ? current.segments.at(i) : 0;
		if (remotePart != currentPart) return remotePart > currentPart;
	}
	const int rr = preRank(remote.pre), cr = preRank(current.pre);
	if (rr != cr) return rr > cr;
	if (!remote.pre.isEmpty() && !current.pre.isEmpty()) return suffixNumber(remote.pre) > suffixNumber(current.pre);
	return false;
}

QString humanSize(qint64 bytes) {
	if (bytes >= 1024LL * 1024LL * 1024LL)
		return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + QStringLiteral(" GB");
	if (bytes <= 0) return QStringLiteral("0 MB");
	return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AppUpdater::AppUpdater(QObject *parent) : QObject(parent) {
	mNetwork = new QNetworkAccessManager(this);
}

AppUpdater::~AppUpdater() {
	if (!mInstallerPath.isEmpty()) QFile::remove(mInstallerPath);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void AppUpdater::checkForUpdate(bool requestedByUser) {
	if (mState == State::Checking || mState == State::Downloading || mState == State::Ready) return;
	mRequestedByUser = requestedByUser;
	mErrorMessage.clear();
	setState(State::Checking);

	const QString api =
	    QStringLiteral("https://api.github.com/repos/") + repo() + QStringLiteral("/releases?per_page=20");
	QNetworkRequest request{QUrl(api)};
	request.setHeader(QNetworkRequest::UserAgentHeader,
	                  QStringLiteral("sufficit-phone-updater/") + QCoreApplication::applicationVersion());
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

	QNetworkReply *reply = mNetwork->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			lWarning() << log().arg(QStringLiteral("Update check failed: %1").arg(reply->errorString()));
			fail(tr("update_error_network"));
			return;
		}
		handleReleasesJson(reply->readAll());
	});
}

void AppUpdater::downloadUpdate() {
	if (mState != State::Available || !mAssetUrl.isValid()) return;
	setState(State::Downloading);
	mReadBytes = 0;
	mTotalBytes = mAssetSize;
	emit downloadProgressChanged();
	fetchChecksumThenDownload();
}

void AppUpdater::cancelDownload() {
	if (mState != State::Downloading) return;
	if (mDownloader) {
		mDownloader->deleteLater(); // aborts the network reply in its destructor
		mDownloader = nullptr;
	}
	mInstallerPath.clear();
	setState(State::Available);
}

void AppUpdater::installUpdate() {
	if (mState != State::Ready || mInstallerPath.isEmpty()) return;
	const QString mode = getInstallMode();
	lInfo() << log().arg(QStringLiteral("Installing update %1 (mode %2)").arg(mAvailableVersion, mode));

	if (mode == QStringLiteral("nsis")) {
		// Silent NSIS install; the installer takes over the running instance.
		if (QProcess::startDetached(mInstallerPath, {QStringLiteral("/S")})) {
			mInstallerPath.clear(); // keep the payload: the updater must not delete it
			QTimer::singleShot(1500, [] { QCoreApplication::exit(0); });
		} else {
			fail(tr("update_error_install"));
		}
	} else if (mode == QStringLiteral("appimage")) {
		if (installAppImage()) {
			mInstallerPath.clear(); // temp copy no longer needed
			emit installFinished();
		}
	} else if (mode == QStringLiteral("dmg")) {
		// macOS cannot safely replace a running bundle: hand the DMG to the user.
		QDesktopServices::openUrl(QUrl::fromLocalFile(mInstallerPath));
		emit manualInstallOpened();
	} else { // "manual": package-managed installs are replaced by the system package manager
		QDesktopServices::openUrl(QUrl(getReleasePageUrl()));
		emit manualInstallOpened();
	}
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

QString AppUpdater::repo() {
	// Overridable for testing / enterprise mirrors.
	const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("SUFFICIT_UPDATE_REPO"));
	return env.isEmpty() ? QString::fromUtf8(Constants::UpdateRepo) : env;
}

QString AppUpdater::getReleasePageUrl() const {
	return QStringLiteral("https://github.com/") + repo() + QStringLiteral("/releases");
}

QString AppUpdater::getInstallMode() const {
#if defined(Q_OS_WIN)
	return QStringLiteral("nsis");
#elif defined(Q_OS_MACOS)
	return QStringLiteral("dmg");
#else
	return QProcessEnvironment::systemEnvironment().contains(QStringLiteral("APPIMAGE")) ? QStringLiteral("appimage")
	                                                                                     : QStringLiteral("manual");
#endif
}

QString AppUpdater::getProgressText() const {
	if (mTotalBytes > 0) return humanSize(mReadBytes) + QStringLiteral(" / ") + humanSize(mTotalBytes);
	return humanSize(mReadBytes);
}

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

void AppUpdater::setState(State state) {
	if (mState == state) return;
	mState = state;
	emit stateChanged();
}

void AppUpdater::fail(const QString &message) {
	mErrorMessage = message;
	lWarning() << log().arg(QStringLiteral("Updater error: %1").arg(message));
	setState(State::Error);
	emit updateError(message, mRequestedByUser);
}

void AppUpdater::handleReleasesJson(const QByteArray &payload) {
	const QJsonDocument doc = QJsonDocument::fromJson(payload);
	if (!doc.isArray()) {
		fail(tr("update_error_network"));
		return;
	}

	// Pre-releases are only offered to clients already running one (or when
	// explicitly enabled), mirroring sufficit-ai-genius behavior.
	const bool allowPrereleases = QCoreApplication::applicationVersion().contains(QLatin1Char('-')) ||
	                              QProcessEnvironment::systemEnvironment().value(
	                                  QStringLiteral("SUFFICIT_UPDATE_ALLOW_PRERELEASE")) == QStringLiteral("1");

	const ParsedVersion current = parseVersion(QCoreApplication::applicationVersion());
	const QString wantedAsset =
#if defined(Q_OS_WIN)
	    QStringLiteral("sufficit-phone-win64.exe");
#elif defined(Q_OS_MACOS)
	    QStringLiteral("sufficit-phone-macos.dmg");
#else
	    QStringLiteral("sufficit-phone-x86_64.AppImage");
#endif

	// The API returns releases newest-first.
	for (const auto &releaseValue : doc.array()) {
		const QJsonObject release = releaseValue.toObject();
		if (release.value(QStringLiteral("draft")).toBool()) continue;
		const bool isPrerelease = release.value(QStringLiteral("prerelease")).toBool();
		if (isPrerelease && !allowPrereleases) continue;

		const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
		QString version = release.value(QStringLiteral("tag_name")).toString();
		if (version.startsWith(QLatin1Char('v'))) version.remove(0, 1);
		// bc_compute_full_version stamps a commit-count based version into the
		// binaries which never matches the tag (tag 6.2.0-alpha.2 -> binary
		// 6.2.0-alpha.185+hash): compare the version embedded in the release
		// assets against the running APPLICATION_SEMVER, keep the tag for display.
		const ParsedVersion candidate = parseVersion(embeddedVersion(assets));
		if (!candidate.valid || !isNewerThan(candidate, current)) continue;

		QString assetUrl, sumsUrl;
		qint64 assetSize = 0;
		for (const auto &assetValue : assets) {
			const QJsonObject asset = assetValue.toObject();
			const QString name = asset.value(QStringLiteral("name")).toString();
			if (name == wantedAsset) {
				assetUrl = asset.value(QStringLiteral("browser_download_url")).toString();
				assetSize = static_cast<qint64>(asset.value(QStringLiteral("size")).toDouble());
			} else if (name == QStringLiteral("SHA256SUMS")) {
				sumsUrl = asset.value(QStringLiteral("browser_download_url")).toString();
			}
		}
		if (assetUrl.isEmpty() || sumsUrl.isEmpty()) continue; // incomplete release, try an older one

		mAvailableVersion = version;
		mAssetName = wantedAsset;
		mAssetUrl = QUrl(assetUrl);
		mSumsUrl = QUrl(sumsUrl);
		mAssetSize = assetSize;
		mInstallerPath.clear();
		emit updateInfoChanged();
		setState(State::Available);
		lInfo() << log().arg(QStringLiteral("Update available: %1 (%2 bytes)").arg(mAvailableVersion).arg(mAssetSize));
		emit updateAvailable(mAvailableVersion, mAssetSize, mRequestedByUser);
		return;
	}

	lInfo() << log().arg(QStringLiteral("No update available (current %1, prereleases %2)")
	                         .arg(QCoreApplication::applicationVersion())
	                         .arg(allowPrereleases));
	setState(State::UpToDate);
	emit upToDate(mRequestedByUser);
}

void AppUpdater::fetchChecksumThenDownload() {
	QNetworkRequest request(mSumsUrl);
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("sufficit-phone-updater"));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

	QNetworkReply *reply = mNetwork->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		if (mState != State::Downloading) return; // cancelled meanwhile
		if (reply->error() != QNetworkReply::NoError) {
			fail(tr("update_error_download"));
			return;
		}
		// SHA256SUMS lines look like: "<sha256>  <asset-name>"
		QString expected;
		const QStringList lines = QString::fromUtf8(reply->readAll()).split(QLatin1Char('\n'));
		for (const QString &line : lines) {
			const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
			if (parts.size() >= 2 && parts.last() == mAssetName) {
				expected = parts.first().toLower();
				break;
			}
		}
		if (expected.isEmpty()) {
			fail(tr("update_error_sums_missing"));
			return;
		}
		startAssetDownload(expected);
	});
}

void AppUpdater::startAssetDownload(const QString &expectedChecksum) {
	const QString folder =
	    QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/sufficit-phone-update");
	QDir().mkpath(folder);

	if (!mInstallerPath.isEmpty()) QFile::remove(mInstallerPath);

	mDownloader = new FileDownloader(this);
	mDownloader->setUrl(mAssetUrl);
	mDownloader->setDownloadFolder(folder);
	mDownloader->setOverwriteFile(true);
	mDownloader->setChecksum(expectedChecksum); // FileDownloader deletes the file on mismatch

	connect(mDownloader, &FileDownloader::readBytesChanged, this, [this](qint64 read) {
		mReadBytes = read;
		emit downloadProgressChanged();
	});
	connect(mDownloader, &FileDownloader::totalBytesChanged, this, [this](qint64 total) {
		mTotalBytes = total;
		emit downloadProgressChanged();
	});
	connect(mDownloader, &FileDownloader::downloadFinished, this, [this](const QString &filePath) {
		mInstallerPath = filePath;
		mDownloader->deleteLater();
		mDownloader = nullptr;
		lInfo() << log().arg(QStringLiteral("Update downloaded and verified: %1").arg(filePath));
		setState(State::Ready);
		emit readyToInstall(mAvailableVersion, mRequestedByUser);
	});
	connect(mDownloader, &FileDownloader::downloadFailed, this, [this] {
		mDownloader->deleteLater();
		mDownloader = nullptr;
		fail(tr("update_error_download"));
	});

	mDownloader->download();
}

bool AppUpdater::installAppImage() {
	const QString appImagePath = QProcessEnvironment::systemEnvironment().value(QStringLiteral("APPIMAGE"));
	if (appImagePath.isEmpty() || !QFileInfo(mInstallerPath).isReadable()) {
		fail(tr("update_error_install"));
		return false;
	}

	// Atomic-ish swap: move the running image aside, copy the new one in place,
	// restore the backup if anything fails. The FUSE mount stays valid until exit.
	const QString backup = appImagePath + QStringLiteral(".old");
	QFile::remove(backup);
	if (!QFile::rename(appImagePath, backup)) {
		// The original is still at appImagePath; never remove it on this path.
		lCritical() << log().arg(QStringLiteral("Could not move current AppImage to backup"));
		fail(tr("update_error_install"));
		return false;
	}
	if (!QFile::copy(mInstallerPath, appImagePath)) {
		lCritical() << log().arg(QStringLiteral("AppImage copy failed; restoring previous image"));
		QFile::remove(appImagePath);
		const bool restored = QFile::rename(backup, appImagePath);
		if (!restored) lCritical() << log().arg(QStringLiteral("CRITICAL: failed to restore backup AppImage"));
		fail(tr("update_error_install"));
		return false;
	}
	QFile::setPermissions(appImagePath, QFile::permissions(backup) | QFile::ExeOwner | QFile::ExeUser |
	                                        QFile::ExeGroup | QFile::ExeOther);
	QFile::remove(backup);

	lInfo() << log().arg(QStringLiteral("AppImage updated in place: %1").arg(appImagePath));
	// App shows the "restart to apply" popup when installFinished arrives.
	return true;
}
