#include "SufficitOAuth.hpp"
#include "tool/Utils.hpp"

#include <QDebug>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

SufficitOAuth::SufficitOAuth(QObject *parent) : QObject(parent) {
	mNetwork = new QNetworkAccessManager(this);
	mPollTimer = new QTimer(this);
	mPollTimer->setInterval(3000);
	connect(mPollTimer, &QTimer::timeout, this, &SufficitOAuth::pollInstallation);
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

	mFlow = new QOAuth2AuthorizationCodeFlow(this);
	mFlow->setAuthorizationUrl(QUrl(QString(AUTHORITY) + "/connect/authorize"));
	mFlow->setTokenUrl(QUrl(QString(AUTHORITY) + "/connect/token"));
	mFlow->setClientIdentifier(CLIENT_ID);
	mFlow->setRequestedScopeTokens({"openid", "profile", "offline_access", SCOPE_INSTALLATION});
	mFlow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
	mFlow->setNetworkAccessManager(mNetwork);

	mFlow->setReplyHandler(mReplyHandler);

	connect(mFlow, &QAbstractOAuth::authorizeWithBrowser, this, [this](const QUrl &url) {
		if (!QDesktopServices::openUrl(url)) {
			qWarning() << "[SufficitOAuth] could not open the system browser";
			failLogin(QStringLiteral("browser_open_failed"));
		}
	});
	connect(mFlow, &QOAuth2AuthorizationCodeFlow::granted, this, &SufficitOAuth::onGranted);
	connect(mFlow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
	        [this](const QString &error, const QString &description, const QUrl &) {
		        qWarning() << "[SufficitOAuth] error:" << error << description;
		        failLogin(description.isEmpty() ? error : description);
	        });
	connect(mFlow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
		qWarning() << "[SufficitOAuth] request failed:" << error;
		failLogin(QStringLiteral("oauth_request_failed"));
	});

	mFlow->grant();
}

void SufficitOAuth::onGranted() {
	mAccessToken = mFlow->token();

	// The loopback listener is needed only for the authorization callback.
	// Releasing the fixed port also lets another app instance authenticate.
	if (mReplyHandler) mReplyHandler->close();
	setLoggingIn(false);

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

	registerInstallation();
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
			emit loginFailed("installation_registration_failed");
			return;
		}
		qInfo() << "[SufficitOAuth] installation registered; HTTP" << status;
		setWaitingForRamal(true);
		mPollTimer->start();
		pollInstallation();
	});
}

void SufficitOAuth::pollInstallation() {
	const QUrl url(QString(PROVISIONING_BASE) + "/installations/" + installationId().toString(QUuid::WithoutBraces));
	auto *reply = mNetwork->get(authorized(url));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) return;

		const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

		// A URL vem pronta do servidor, com um bilhete de uso unico e vida
		// curta. Montar o endereco aqui a partir do identificador do ramal era
		// o que fazia daquele identificador uma credencial permanente: quem
		// visse a URL uma vez buscaria a senha SIP para sempre.
		const QString provisioningUrl = obj.value("provisioningUrl").toString();
		if (provisioningUrl.isEmpty()) return;

		mPollTimer->stop();
		setWaitingForRamal(false);

		Utils::useFetchConfig(QString(PROVISIONING_BASE) + provisioningUrl);
		emit ramalReady();
	});
}
