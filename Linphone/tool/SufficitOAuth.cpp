#include "SufficitOAuth.hpp"
#include "tool/Utils.hpp"

#include <qtkeychain/keychain.h>

#include "config.h"

#include <QDebug>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSysInfo>
#include <QUrl>

SufficitOAuth::SufficitOAuth(QObject *parent) : QObject(parent) {
	mNetwork = new QNetworkAccessManager(this);

	mPollTimer = new QTimer(this);
	mPollTimer->setInterval(3000);
	connect(mPollTimer, &QTimer::timeout, this, &SufficitOAuth::pollInstallation);

	mHeartbeatTimer = new QTimer(this);
	mHeartbeatTimer->setInterval(HEARTBEAT_INTERVAL_MS);
	connect(mHeartbeatTimer, &QTimer::timeout, this, &SufficitOAuth::sendHeartbeat);

	QSettings settings("Sufficit", "SufficitPhone");
	const QVariant applied = settings.value("appliedConfigVersion");
	mAppliedConfigVersion = applied.isValid() ? applied.toLongLong() : -1;
}

void SufficitOAuth::setLoggingIn(bool value) {
	if (mLoggingIn == value) return;
	mLoggingIn = value;
	emit loggingInChanged();
}

void SufficitOAuth::setWaitingForRamal(bool value) {
	if (mWaitingForRamal == value) return;
	mWaitingForRamal = value;
	emit waitingForRamalChanged();
}

void SufficitOAuth::setResuming(bool value) {
	if (mResuming == value) return;
	mResuming = value;
	emit resumingChanged();
}

QUuid SufficitOAuth::installationId() {
	QSettings settings("Sufficit", "SufficitPhone");
	QString stored = settings.value("installationId").toString();
	QUuid id(stored);
	if (id.isNull()) {
		id = QUuid::createUuid();
		settings.setValue("installationId", id.toString(QUuid::WithoutBraces));
	}
	return id;
}

QNetworkRequest SufficitOAuth::authorized(const QUrl &url) const {
	QNetworkRequest request(url);
	if (!mAccessToken.isEmpty()) request.setRawHeader("Authorization", ("Bearer " + mAccessToken).toUtf8());
	return request;
}

QString SufficitOAuth::platformName() const {
#if defined(Q_OS_WIN)
	return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
	return QStringLiteral("macos");
#else
	// productType() keeps the distribution ("ubuntu", "fedora", ...), which is
	// what a fleet view actually wants to see.
	return QSysInfo::productType();
#endif
}

// -----------------------------------------------------------------------------
// Refresh token persistence (OS credential store via QtKeychain)
// -----------------------------------------------------------------------------

void SufficitOAuth::storeRefreshToken(const QString &refreshToken) {
	if (refreshToken.isEmpty()) return;
	auto *job = new QKeychain::WritePasswordJob(QLatin1String(KEYCHAIN_SERVICE));
	job->setAutoDelete(true);
	job->setKey(QLatin1String(KEYCHAIN_REFRESH_TOKEN_KEY));
	job->setTextData(refreshToken);
	connect(job, &QKeychain::Job::finished, this, [](QKeychain::Job *finished) {
		if (finished->error() != QKeychain::NoError) {
			// Not persisted this time; the app still works, only the next
			// restart will need an interactive login instead of a silent one.
			qWarning() << "[SufficitOAuth] could not store the refresh token in the OS keychain:"
			           << finished->errorString();
		}
	});
	job->start();
}

void SufficitOAuth::readRefreshToken(std::function<void(const QString &)> callback) {
	auto *job = new QKeychain::ReadPasswordJob(QLatin1String(KEYCHAIN_SERVICE));
	job->setAutoDelete(true);
	job->setKey(QLatin1String(KEYCHAIN_REFRESH_TOKEN_KEY));
	connect(job, &QKeychain::Job::finished, this, [callback](QKeychain::Job *finished) {
		auto *read = static_cast<QKeychain::ReadPasswordJob *>(finished);
		if (read->error() != QKeychain::NoError) {
			// EntryNotFound on first run/after sign-out; any other code means
			// the platform has no usable secure backend (e.g. headless Linux
			// without a Secret Service). Either way there is nothing to resume.
			callback(QString());
			return;
		}
		callback(read->textData());
	});
	job->start();
}

void SufficitOAuth::deleteRefreshToken() {
	auto *job = new QKeychain::DeletePasswordJob(QLatin1String(KEYCHAIN_SERVICE));
	job->setAutoDelete(true);
	job->setKey(QLatin1String(KEYCHAIN_REFRESH_TOKEN_KEY));
	connect(job, &QKeychain::Job::finished, this, [](QKeychain::Job *finished) {
		if (finished->error() != QKeychain::NoError && finished->error() != QKeychain::EntryNotFound) {
			qWarning() << "[SufficitOAuth] could not delete the stored refresh token:" << finished->errorString();
		}
	});
	job->start();
}

// -----------------------------------------------------------------------------
// OAuth flow
// -----------------------------------------------------------------------------

bool SufficitOAuth::ensureReplyHandlerListening() {
	if (!mReplyHandler) {
		mReplyHandler = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, REDIRECT_PORT, this);
	} else if (!mReplyHandler->isListening()) {
		mReplyHandler->listen(QHostAddress::LocalHost, REDIRECT_PORT);
	}

	const QUrl callbackUrl(mReplyHandler->callback());
	const bool callbackReady = mReplyHandler->isListening() && mReplyHandler->port() == REDIRECT_PORT &&
	                           callbackUrl.scheme() == QStringLiteral("http") &&
	                           callbackUrl.host() == QStringLiteral("127.0.0.1");
	if (!callbackReady) {
		qWarning() << "[SufficitOAuth] callback port" << REDIRECT_PORT << "unavailable:" << callbackUrl;
		return false;
	}

	qInfo() << "[SufficitOAuth] redirect callback:" << callbackUrl;
	return true;
}

void SufficitOAuth::failLogin(const QString &reason) {
	// More than one Qt OAuth signal can describe the same failure. The state
	// guard keeps the UI and diagnostics from receiving duplicate failures.
	if (!mLoggingIn) return;
	if (mReplyHandler) mReplyHandler->close();
	setLoggingIn(false);
	emit loginFailed(reason);
}

void SufficitOAuth::handleSilentFailure(const QString &error) {
	setResuming(false);
	if (error == QLatin1String("invalid_grant")) {
		// The stored refresh token was revoked or expired: keeping it around
		// would only fail the same way on every future start and on every
		// heartbeat. Forgetting it sends the user straight to interactive
		// login instead of a silent retry loop that can never succeed.
		qWarning() << "[SufficitOAuth] refresh token rejected; forgetting the stored session";
		deleteRefreshToken();
		mAccessToken.clear();
		mRegistered = false;
		if (mHeartbeatTimer) mHeartbeatTimer->stop();
		mPollTimer->stop();
		setWaitingForRamal(false);
	}
}

void SufficitOAuth::configureFlow(bool interactive) {
	mFlow = new QOAuth2AuthorizationCodeFlow(this);
	mFlow->setAuthorizationUrl(QUrl(QString(AUTHORITY) + "/connect/authorize"));
	mFlow->setTokenUrl(QUrl(QString(AUTHORITY) + "/connect/token"));
	mFlow->setClientIdentifier(CLIENT_ID);
	mFlow->setRequestedScopeTokens({"openid", "profile", "offline_access", SCOPE_INSTALLATION});
	mFlow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
	mFlow->setNetworkAccessManager(mNetwork);
	// Keep the access token fresh without user interaction: a session that
	// only ever renews through resumeSession() would still drop every call
	// made between two app restarts once the short-lived access token expires.
	mFlow->setAutoRefresh(true);

	if (interactive) {
		mFlow->setReplyHandler(mReplyHandler);
		connect(mFlow, &QAbstractOAuth::authorizeWithBrowser, this, [this](const QUrl &url) {
			if (!QDesktopServices::openUrl(url)) {
				qWarning() << "[SufficitOAuth] could not open the system browser";
				failLogin(QStringLiteral("browser_open_failed"));
			}
		});
	}

	connect(mFlow, &QOAuth2AuthorizationCodeFlow::granted, this, &SufficitOAuth::onGranted);
	connect(mFlow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
	        [this](const QString &error, const QString &description, const QUrl &) {
		        qWarning() << "[SufficitOAuth] error:" << error << description;
		        if (mLoggingIn) failLogin(description.isEmpty() ? error : description);
		        else handleSilentFailure(error);
	        });
	connect(mFlow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
		qWarning() << "[SufficitOAuth] request failed:" << error;
		if (mLoggingIn) failLogin(QStringLiteral("oauth_request_failed"));
		else handleSilentFailure(QString());
	});
	// An access token changes both after the authorization-code grant and
	// every successful refresh. Keep the request credential synchronized with
	// QAbstractOAuth2 before the next provisioning/heartbeat request.
	connect(mFlow, &QAbstractOAuth::tokenChanged, this, [this](const QString &token) { mAccessToken = token; });
	// IdentityServer may rotate the refresh token on use; persisting the
	// rotated value is what keeps the next restart able to resume silently.
	connect(mFlow, &QAbstractOAuth2::refreshTokenChanged, this,
	        [this](const QString &refreshToken) { storeRefreshToken(refreshToken); });
}

void SufficitOAuth::login() {
	if (mLoggingIn) return;
	setLoggingIn(true);

	// A new flow must not remain connected to the callback handler alongside
	// an obsolete one. The handler itself is persistent: replacing it through
	// deleteLater() used to make both instances contend for port 47623 and Qt
	// then generated an invalid redirect_uri on repeated login attempts.
	delete mFlow;
	mFlow = nullptr;

	if (!ensureReplyHandlerListening()) {
		failLogin(QStringLiteral("callback_port_unavailable"));
		return;
	}

	configureFlow(true);
	mFlow->grant();
}

void SufficitOAuth::resumeSession() {
	if (mLoggingIn || mResuming || mRegistered) return;

	readRefreshToken([this](const QString &refreshToken) {
		// No stored token: first run, signed out, or the OS has no usable
		// credential store. Interactive login remains the only path - a
		// plaintext fallback here would defeat the point of the keychain.
		if (refreshToken.isEmpty()) return;

		setResuming(true);
		delete mFlow;
		mFlow = nullptr;
		configureFlow(false);
		// The refresh-token grant is a single direct POST to the token
		// endpoint: no browser, no loopback handler, no user action.
		mFlow->setRefreshToken(refreshToken);
		mFlow->refreshTokens();
	});
}

void SufficitOAuth::signOut() {
	// Local sign-out: the stored session is forgotten and the provisioning
	// channel stops. Revoking the token server-side is a roadmap item; until
	// then the refresh token simply dies at its natural expiry.
	mPollTimer->stop();
	if (mHeartbeatTimer) mHeartbeatTimer->stop();
	mRegistered = false;
	mAccessToken.clear();
	setWaitingForRamal(false);
	deleteRefreshToken();

	QSettings settings("Sufficit", "SufficitPhone");
	settings.remove("appliedConfigVersion");
	mAppliedConfigVersion = -1;
}

void SufficitOAuth::onGranted() {
	mAccessToken = mFlow->token();

	// The loopback listener is needed only for the authorization callback.
	// Releasing the fixed port also lets another app instance authenticate.
	if (mReplyHandler) mReplyHandler->close();
	setLoggingIn(false);
	setResuming(false);

	if (mAccessToken.isEmpty()) {
		qWarning() << "[SufficitOAuth] no access token in the grant";
		emit loginFailed("missing_access_token");
		return;
	}

	// Never log the credential itself. The shape is enough to diagnose a
	// resource-server contract mismatch: signed JWTs have three segments,
	// while OpenIddict reference tokens are opaque.
	qInfo() << "[SufficitOAuth] access token format:"
	        << (mAccessToken.count(QLatin1Char('.')) == 2 ? "jwt" : "reference");

	if (!mFlow->refreshToken().isEmpty()) storeRefreshToken(mFlow->refreshToken());

	if (!mRegistered) registerInstallation();
}

void SufficitOAuth::registerInstallation() {
	QNetworkRequest request = authorized(QUrl(QString(PROVISIONING_BASE) + "/installations"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	// Sem userId no corpo: quem o servidor atende vem do "sub" do token, que
	// e assinado. Mandar o identificador aqui seria pedir para o servidor
	// acreditar no cliente.
	QJsonObject body;
	body["installationId"] = installationId().toString(QUuid::WithoutBraces);

	auto *reply = mNetwork->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError) {
			const QByteArray challenge = reply->rawHeader("WWW-Authenticate");
			qWarning() << "[SufficitOAuth] failed to register installation; HTTP" << status << reply->errorString()
			           << "challenge:" << challenge;
			if (mLoggingIn || !mRegistered) emit loginFailed("installation_registration_failed");
			return;
		}
		qInfo() << "[SufficitOAuth] installation registered; HTTP" << status;
		mRegistered = true;
		startHeartbeat();
		sendHeartbeat();
		pollInstallation();
	});
}

void SufficitOAuth::pollInstallation() {
	QString path = QString(PROVISIONING_BASE) + "/installations/" + installationId().toString(QUuid::WithoutBraces);
	// Telling the server what this client already applied lets it skip
	// issuing a new ticket when nothing changed: without this parameter every
	// poll - including the periodic ones after a silent resume - would look
	// identical to a first-time login and hand out a fresh, if unused, ticket.
	if (mAppliedConfigVersion >= 0) path += QStringLiteral("?appliedVersion=") + QString::number(mAppliedConfigVersion);

	auto *reply = mNetwork->get(authorized(QUrl(path)));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status == 404) {
				// The installation row is gone server-side (rare, manual
				// cleanup): stop hammering an endpoint that will never
				// answer differently. The next login registers it again.
				qWarning() << "[SufficitOAuth] installation no longer known by the server";
				mPollTimer->stop();
				mRegistered = false;
				setWaitingForRamal(false);
				return;
			}
			// Transient network/server error: keep the polling loop alive so
			// login (which relies on it to reach the assigned ramal) is not
			// stuck without a retry.
			if (!mPollTimer->isActive()) mPollTimer->start();
			return;
		}

		const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
		const bool assigned = !obj.value("assignedRamalId").toString().isEmpty();

		if (!assigned) {
			// Still waiting for the user to pick a ramal on the website.
			setWaitingForRamal(true);
			if (!mPollTimer->isActive()) mPollTimer->start();
			return;
		}

		mPollTimer->stop();
		setWaitingForRamal(false);

		const qint64 configVersion = (qint64)obj.value("configVersion").toDouble();
		const QString provisioningUrl = obj.value("provisioningUrl").toString();
		if (!provisioningUrl.isEmpty()) {
			// A URL vem pronta do servidor, com um bilhete de uso unico e vida
			// curta. Montar o endereco aqui a partir do identificador do ramal
			// era o que fazia daquele identificador uma credencial permanente:
			// quem visse a URL uma vez buscaria a senha SIP para sempre.
			Utils::useFetchConfig(QString(PROVISIONING_BASE) + provisioningUrl);

			mAppliedConfigVersion = configVersion;
			QSettings settings("Sufficit", "SufficitPhone");
			settings.setValue("appliedConfigVersion", (qlonglong)mAppliedConfigVersion);
			reportApplied(configVersion);
		}

		emit ramalReady();
	});
}

void SufficitOAuth::reportApplied(qint64 configVersion) {
	QNetworkRequest request = authorized(QUrl(QString(PROVISIONING_BASE) + "/installations/" +
	                                          installationId().toString(QUuid::WithoutBraces) + "/applied"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	QJsonObject body;
	body["configVersion"] = (double)configVersion;

	auto *reply = mNetwork->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [reply]() {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			// A 409 here just means the server published a newer version
			// meanwhile; the next poll picks it up. Nothing for the user to
			// act on, so this stays a diagnostic log rather than a signal.
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			qWarning() << "[SufficitOAuth] failed to report applied config version; HTTP" << status
			           << reply->errorString();
		}
	});
}

// -----------------------------------------------------------------------------
// Heartbeat
// -----------------------------------------------------------------------------

void SufficitOAuth::startHeartbeat() {
	if (!mHeartbeatTimer->isActive()) mHeartbeatTimer->start();
}

void SufficitOAuth::sendHeartbeat() {
	if (mAccessToken.isEmpty()) return;

	QNetworkRequest request = authorized(QUrl(QString(PROVISIONING_BASE) + "/installations/" +
	                                          installationId().toString(QUuid::WithoutBraces) + "/heartbeat"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	QJsonObject body;
	body["clientVersion"] = QStringLiteral(APPLICATION_SEMVER);
	body["clientPlatform"] = platformName();

	auto *reply = mNetwork->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status == 401 || status == 403) {
			// The access token died between refreshes: ask the flow for a new
			// one. If the refresh token is gone too, serverReportedErrorOccurred
			// lands in handleSilentFailure(), which forgets the session and
			// stops this timer - so this loop cannot spin forever on a dead
			// credential.
			if (mFlow && !mFlow->refreshToken().isEmpty()) mFlow->refreshTokens();
			return;
		}
		if (reply->error() != QNetworkReply::NoError) {
			qWarning() << "[SufficitOAuth] heartbeat failed; HTTP" << status << reply->errorString();
		}
	});
}
