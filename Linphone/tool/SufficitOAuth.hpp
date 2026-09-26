#ifndef SUFFICIT_OAUTH_H_
#define SUFFICIT_OAUTH_H_

#include <QNetworkAccessManager>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUuid>

#include <functional>

// =============================================================================
// Sufficit OAuth login (identity.sufficit.com.br) + installation registration.
//
// First login uses Authorization Code + PKCE through the system browser. The
// access token is sent as a Bearer credential; the provisioning server derives
// the user from the signed token. The client then registers its installation
// and polls until the user assigns a SIP ramal.
//
// Session persistence: the refresh token returned by offline_access is stored
// in the OS credential store (Windows Credential Manager, macOS Keychain, Linux
// Secret Service/KWallet - see external/qtkeychain), never in plain settings.
// resumeSession() performs a silent refresh-token grant after the Linphone core
// starts and resumes the same registration/polling flow without a browser.
//
// Heartbeat: while registered, the app posts version/platform liveness to
// /installations/{id}/heartbeat every 15 minutes. Polling reports the last
// applied config version so an up-to-date installation consumes no ticket.
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
	Q_PROPERTY(bool resuming READ resuming NOTIFY resumingChanged)

public:
	explicit SufficitOAuth(QObject *parent = nullptr);

	bool loggingIn() const { return mLoggingIn; }
	bool waitingForRamal() const { return mWaitingForRamal; }
	bool resuming() const { return mResuming; }

	Q_INVOKABLE void login();
	Q_INVOKABLE void resumeSession();
	// Local sign-out only. Server-side revocation is a separate roadmap item.
	Q_INVOKABLE void signOut();

signals:
	void loggingInChanged();
	void waitingForRamalChanged();
	void resumingChanged();
	void loginFailed(const QString &reason);
	void ramalReady();

private:
	void configureFlow(bool interactive);
	void onGranted();
	void registerInstallation();
	void pollInstallation();
	void reportApplied(qint64 configVersion);
	void startHeartbeat();
	void sendHeartbeat();
	void handleSilentFailure(const QString &error);
	bool ensureReplyHandlerListening();
	void failLogin(const QString &reason);
	void setLoggingIn(bool value);
	void setWaitingForRamal(bool value);
	void setResuming(bool value);
	QString platformName() const;

	void storeRefreshToken(const QString &refreshToken);
	void readRefreshToken(std::function<void(const QString &)> callback);
	void deleteRefreshToken();

	QNetworkRequest authorized(const QUrl &url) const;
	static QUuid installationId();

	QOAuth2AuthorizationCodeFlow *mFlow = nullptr;
	QOAuthHttpServerReplyHandler *mReplyHandler = nullptr;
	QNetworkAccessManager *mNetwork = nullptr;
	QTimer *mPollTimer = nullptr;
	QTimer *mHeartbeatTimer = nullptr;

	bool mLoggingIn = false;
	bool mWaitingForRamal = false;
	bool mResuming = false;
	bool mRegistered = false;
	qint64 mAppliedConfigVersion = -1;
	QString mAccessToken;

	static constexpr quint16 REDIRECT_PORT = 47623;
	static constexpr const char *AUTHORITY = "https://identity.sufficit.com.br";
	static constexpr const char *CLIENT_ID = "sufficit-phone-desktop";
	// offline_access persists the desktop session; the narrow installation
	// scope remains distinct from provisioning.manage.
	static constexpr const char *SCOPE_INSTALLATION = "provisioning.installation";
	static constexpr const char *PROVISIONING_BASE = "https://provisioning.sufficit.com.br:26511";
	static constexpr const char *KEYCHAIN_SERVICE = "SufficitPhone";
	static constexpr const char *KEYCHAIN_REFRESH_TOKEN_KEY = "oauth-refresh-token";
	static constexpr int HEARTBEAT_INTERVAL_MS = 15 * 60 * 1000;
};

#endif // SUFFICIT_OAUTH_H_
