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
//   2. The access token is kept and sent as a Bearer credential; the server
//      derives the user from the signed token.
//   3. A locally-persisted installation id is registered on the provisioning
//      API (POST /installations).
//   4. The app polls the same API (GET /installations/{id}) until the user
//      picks a SIP ramal for this installation on the Sufficit website.
//   5. Once assigned, Utils::useFetchConfig() is called with the existing
//      /linphone/endpoint provisioning URL for that ramal - reusing the
//      remote-provisioning mechanism already used by the app.
//
// The identity is NEVER asserted by this client. An earlier version decoded
// the id_token's payload with base64 - no signature, no issuer, no audience,
// no expiry, no nonce - and then posted that "sub" as the user id, which the
// server had no choice but to believe. OpenID Connect Core section 3.1.3.7
// requires that validation, and doing it here would only duplicate what the
// resource server already does when it validates the access token. Sending
// the token instead means the caller proves who it is rather than claiming it.
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
	void registerInstallation();
	void pollInstallation();
	void setLoggingIn(bool value);
	void setWaitingForRamal(bool value);

	// Carimba o token de acesso na requisicao. Toda chamada ao provisioning
	// passa por aqui para que nenhuma escape sem credencial.
	QNetworkRequest authorized(const QUrl &url) const;

	static QUuid installationId();

	QOAuth2AuthorizationCodeFlow *mFlow = nullptr;
	QOAuthHttpServerReplyHandler *mReplyHandler = nullptr;
	QNetworkAccessManager *mNetwork = nullptr;
	QTimer *mPollTimer = nullptr;

	bool mLoggingIn = false;
	bool mWaitingForRamal = false;
	QString mAccessToken;

	static constexpr quint16 REDIRECT_PORT = 47623;
	static constexpr const char *AUTHORITY = "https://identity.sufficit.com.br";
	static constexpr const char *CLIENT_ID = "sufficit-phone-desktop";
	// Escopo estreito: registrar a propria instalacao. Deliberadamente separado
	// de provisioning.manage, que administra o parque inteiro. offline_access
	// traz o refresh token - um softphone precisa continuar registrado depois
	// que o token de acesso expira.
	static constexpr const char *SCOPE_INSTALLATION = "provisioning.installation";
	static constexpr const char *PROVISIONING_BASE = "https://provisioning.sufficit.com.br:26511";
};

#endif // SUFFICIT_OAUTH_H_
