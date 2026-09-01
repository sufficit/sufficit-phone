#include "SufficitOAuth.hpp"
#include "tool/Utils.hpp"

#include <QDebug>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrlQuery>

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

void SufficitOAuth::login() {
	if (mLoggingIn) return;
	setLoggingIn(true);

	if (mFlow) mFlow->deleteLater();
	if (mReplyHandler) mReplyHandler->deleteLater();

	mFlow = new QOAuth2AuthorizationCodeFlow(this);
	mFlow->setAuthorizationUrl(QUrl(QString(AUTHORITY) + "/connect/authorize"));
	mFlow->setTokenUrl(QUrl(QString(AUTHORITY) + "/connect/token"));
	mFlow->setClientIdentifier(CLIENT_ID);
	mFlow->setRequestedScopeTokens({"openid", "profile", "offline_access", SCOPE_INSTALLATION});
	mFlow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
	mFlow->setNetworkAccessManager(mNetwork);

	mReplyHandler = new QOAuthHttpServerReplyHandler(REDIRECT_PORT, this);
	mFlow->setReplyHandler(mReplyHandler);

	connect(mFlow, &QAbstractOAuth::authorizeWithBrowser, this,
	        [](const QUrl &url) { QDesktopServices::openUrl(url); });
	connect(mFlow, &QOAuth2AuthorizationCodeFlow::granted, this, &SufficitOAuth::onGranted);
	connect(mFlow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
	        [this](const QString &error, const QString &description, const QUrl &) {
		        qWarning() << "[SufficitOAuth] error:" << error << description;
		        setLoggingIn(false);
		        emit loginFailed(description.isEmpty() ? error : description);
	        });

	mFlow->grant();
}

void SufficitOAuth::onGranted() {
	mAccessToken = mFlow->token();

	setLoggingIn(false);

	if (mAccessToken.isEmpty()) {
		qWarning() << "[SufficitOAuth] no access token in the grant";
		emit loginFailed("missing_access_token");
		return;
	}

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
		if (reply->error() != QNetworkReply::NoError) {
			qWarning() << "[SufficitOAuth] failed to register installation:" << reply->errorString();
			emit loginFailed("installation_registration_failed");
			return;
		}
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
		const QString ramalId = obj.value("assignedRamalId").toString();
		if (ramalId.isEmpty()) return;

		mPollTimer->stop();
		setWaitingForRamal(false);

		const QString configUrl = QString(PROVISIONING_BASE) + "/linphone/endpoint?id=" + ramalId;
		Utils::useFetchConfig(configUrl);
		emit ramalReady();
	});
}
