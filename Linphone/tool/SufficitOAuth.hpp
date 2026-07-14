#ifndef SUFFICIT_OAUTH_H_
#define SUFFICIT_OAUTH_H_

#include <QNetworkAccessManager>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUuid>

// =============================================================================
// Sufficit OAuth login (identity.sufficit.com.br) + installation registration.
//
// Flow:
//   1. login() opens the system browser on the Sufficit IdentityServer
//      authorize page (Authorization Code + PKCE, loopback redirect).
//   2. On success, the id_token's "sub" claim gives the Sufficit user id.
//   3. A locally-persisted installation id is registered against that user
//      on the provisioning API (POST /installations).
//   4. The app polls the same API (GET /installations/{id}) until the user
//      picks a SIP ramal for this installation on the Sufficit website.
//   5. Once assigned, Utils::useFetchConfig() is called with the existing
//      /linphone/endpoint provisioning URL for that ramal - reusing the
//      remote-provisioning mechanism already used by the app.
// =============================================================================

class SufficitOAuth : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool loggingIn READ loggingIn NOTIFY loggingInChanged)
	Q_PROPERTY(bool waitingForRamal READ waitingForRamal NOTIFY waitingForRamalChanged)

public:
	explicit SufficitOAuth(QObject *parent = nullptr);

	bool loggingIn() const {
		return mLoggingIn;
	}
	bool waitingForRamal() const {
		return mWaitingForRamal;
	}

	Q_INVOKABLE void login();

signals:
	void loggingInChanged();
	void waitingForRamalChanged();
	void loginFailed(const QString &reason);
	void ramalReady();

private:
	void onGranted();
	void registerInstallation(const QString &userId);
	void pollInstallation();
	void setLoggingIn(bool value);
	void setWaitingForRamal(bool value);

	static QString decodeJwtSubject(const QString &jwt);
	static QUuid installationId();

	QOAuth2AuthorizationCodeFlow *mFlow = nullptr;
	QOAuthHttpServerReplyHandler *mReplyHandler = nullptr;
	QNetworkAccessManager *mNetwork = nullptr;
	QTimer *mPollTimer = nullptr;

	bool mLoggingIn = false;
	bool mWaitingForRamal = false;

	static constexpr quint16 REDIRECT_PORT = 47623;
	static constexpr const char *AUTHORITY = "https://identity.sufficit.com.br";
	static constexpr const char *CLIENT_ID = "sufficit-phone-desktop";
	static constexpr const char *PROVISIONING_BASE = "https://provisioning.sufficit.com.br:26511";
};

#endif // SUFFICIT_OAUTH_H_
