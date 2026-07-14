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

QString SufficitOAuth::decodeJwtSubject(const QString &jwt) {
	const QStringList parts = jwt.split('.');
	if (parts.size() < 2) return QString();

	QString payload = parts[1];
	payload.replace('-', '+').replace('_', '/');
	while (payload.size() % 4 != 0)
		payload.append('=');

	const QByteArray json = QByteArray::fromBase64(payload.toUtf8());
	const QJsonObject obj = QJsonDocument::fromJson(json).object();
	return obj.value("sub").toString();
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
	mFlow->setRequestedScopeTokens({"openid", "profile"});
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
	const QString idToken = mFlow->extraTokens().value("id_token").toString();
	const QString userId = decodeJwtSubject(idToken);

	setLoggingIn(false);

	if (userId.isEmpty()) {
		qWarning() << "[SufficitOAuth] could not extract user id from id_token";
		emit loginFailed("missing_user_id");
		return;
	}

	registerInstallation(userId);
}

void SufficitOAuth::registerInstallation(const QString &userId) {
	QNetworkRequest request(QUrl(QString(PROVISIONING_BASE) + "/installations"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	QJsonObject body;
	body["installationId"] = installationId().toString(QUuid::WithoutBraces);
	body["userId"] = userId;

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
	auto *reply = mNetwork->get(QNetworkRequest(url));
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
