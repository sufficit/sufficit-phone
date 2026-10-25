/*
 * Sufficit Phone for Android - Sufficit sign-in and installation provisioning.
 *
 * Port of Linphone/tool/SufficitOAuth.cpp (desktop) to the Android app:
 *  - Authorization Code + PKCE (S256) against identity.sufficit.com.br, through
 *    the system browser and a loopback redirect (RFC 8252 section 7.3). The
 *    loopback URI is the one already registered for the Sufficit Phone public
 *    client, so no client secret exists anywhere in the APK.
 *  - The access token is sent as a Bearer credential to the provisioning
 *    service, which derives the user from the signed token. The client never
 *    asserts its own identity.
 *  - The installation registers itself, polls until a ramal is assigned and
 *    applies the one-shot provisioning URL issued by the server.
 *  - The refresh token is kept only in EncryptedSharedPreferences (Android
 *    Keystore backed). There is no plaintext fallback: without a secure store
 *    the session simply lasts until the process dies.
 *  - While signed in, a heartbeat reports version/platform every 15 minutes and
 *    checks for a newer published configuration.
 */
package org.linphone.sufficit

import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.Base64
import androidx.lifecycle.MutableLiveData
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONObject
import org.linphone.BuildConfig
import org.linphone.LinphoneApplication.Companion.coreContext
import org.linphone.core.Account
import org.linphone.core.AuthInfo
import org.linphone.core.AuthMethod
import org.linphone.core.ConfiguringState
import org.linphone.core.RegistrationState
import org.linphone.core.Core
import org.linphone.core.CoreListenerStub
import org.linphone.core.tools.Log

object SufficitAuth {
    private const val TAG = "[Sufficit Auth]"

    private const val AUTHORITY = "https://identity.sufficit.com.br"
    private const val CLIENT_ID = "sufficit-phone-desktop"
    private const val REDIRECT_PORT = 47623
    private const val REDIRECT_URI = "http://127.0.0.1:47623/"
    private const val SCOPES = "openid profile offline_access provisioning.installation"
    private const val PROVISIONING_BASE = "https://provisioning.sufficit.com.br:26511"

    private const val POLL_INTERVAL_MS = 3_000L
    private const val HEARTBEAT_INTERVAL_MS = 15 * 60 * 1000L
    private const val LOGIN_TIMEOUT_MS = 5 * 60 * 1000L

    private const val SECURE_PREFS = "sufficit_auth_secure"
    private const val PLAIN_PREFS = "sufficit_auth"
    private const val KEY_REFRESH_TOKEN = "refresh_token"
    private const val KEY_INSTALLATION_ID = "installation_id"
    private const val KEY_APPLIED_VERSION = "applied_config_version"

    const val RETURN_SCHEME = "sufficitphone"
    private const val RETURN_INTENT =
        "intent://login#Intent;scheme=$RETURN_SCHEME;package=com.sufficit.phone;end"

    enum class State {
        SIGNED_OUT,
        AUTHORIZING,
        RESUMING,
        REGISTERING,
        WAITING_FOR_RAMAL,
        APPLYING,
        READY,
        FAILED
    }

    val state = MutableLiveData(State.SIGNED_OUT)
    val message = MutableLiveData<String?>(null)

    private lateinit var appContext: Context
    private var securePrefs: SharedPreferences? = null
    private lateinit var plainPrefs: SharedPreferences
    private val executor = Executors.newSingleThreadScheduledExecutor()
    private var pollTask: ScheduledFuture<*>? = null
    private var heartbeatTask: ScheduledFuture<*>? = null
    private var initialized = false

    @Volatile private var refreshToken: String? = null

    @Volatile private var accessToken: String? = null

    @Volatile private var accessTokenExpiresAt = 0L

    @Volatile private var registered = false

    @Volatile private var pendingConfigVersion = -1L

    @Volatile private var loginAttempt = 0

    private var coreListenerAdded = false

    // Diagnostics: never logs secrets, only presence/length of credentials.
    // Written straight to logcat because the Linphone logger is silent in release builds.
    private fun diag(message: String) {
        android.util.Log.i("SufficitDiag", message)
        Log.i("$TAG $message")
    }

    private fun describeAuthInfo(info: AuthInfo): String =
        "username=${info.username} userid=${info.userid} realm=${info.realm} domain=${info.domain} " +
            "algorithm=${info.algorithm} password=${info.password?.let { "<${it.length} chars>" } ?: "<none>"} " +
            "ha1=${info.ha1?.let { "<${it.length} chars>" } ?: "<none>"}"

    private fun dumpSipSetup(core: Core, reason: String) {
        diag("[diag] $reason: ${core.authInfoList.size} auth info(s), ${core.accountList.size} account(s)")
        core.authInfoList.forEach { diag("[diag] stored auth: ${describeAuthInfo(it)}") }
        core.accountList.forEach { account ->
            val params = account.params
            diag("[diag] account identity=${params.identityAddress?.asStringUriOnly()} " +
                    "server=${params.serverAddress?.asStringUriOnly()} realm=${params.realm} " +
                    "transport=${params.transport} state=${account.state}"
            )
        }
    }

    private val coreListener = object : CoreListenerStub() {
        override fun onAuthenticationRequested(core: Core, authInfo: AuthInfo, method: AuthMethod) {
            diag("[diag] Server requested authentication ($method): ${describeAuthInfo(authInfo)}")
            dumpSipSetup(core, "on authentication request")
        }

        override fun onAccountRegistrationStateChanged(
            core: Core,
            account: Account,
            state: RegistrationState?,
            message: String
        ) {
            val error = account.errorInfo
            diag("[diag] Registration ${account.params.identityAddress?.asStringUriOnly()} -> $state " +
                    "[$message] reason=${error?.reason} protocolCode=${error?.protocolCode} phrase=${error?.phrase}"
            )
        }

        override fun onConfiguringStatus(core: Core, status: ConfiguringState?, message: String?) {
            if (status == ConfiguringState.Successful) dumpSipSetup(core, "after provisioning")
            val version = pendingConfigVersion
            if (version < 0) return
            if (status == ConfiguringState.Successful) {
                // The provisioning URL carries a single-use, short-lived ticket.
                // Leaving it configured would make every later Core start fetch
                // a dead ticket and report a provisioning failure.
                core.provisioningUri = null
                pendingConfigVersion = -1
                executor.execute { onConfigurationApplied(version) }
            } else if (status == ConfiguringState.Failed) {
                core.provisioningUri = null
                pendingConfigVersion = -1
                Log.e("$TAG Remote provisioning failed: $message")
                fail("Não foi possível aplicar a configuração do ramal. Tente novamente.")
            }
        }
    }

    // ---------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------

    @Synchronized
    fun init(context: Context) {
        if (initialized) return
        initialized = true
        appContext = context.applicationContext
        plainPrefs = appContext.getSharedPreferences(PLAIN_PREFS, Context.MODE_PRIVATE)
        securePrefs = try {
            val masterKey = MasterKey.Builder(appContext)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build()
            EncryptedSharedPreferences.create(
                appContext,
                SECURE_PREFS,
                masterKey,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
            )
        } catch (e: Exception) {
            // No secure store: no persistence, never a plaintext fallback.
            Log.e("$TAG Secure storage unavailable, the session will not survive restarts: $e")
            null
        }
        coreContext.postOnCoreThread { core ->
            if (!coreListenerAdded) {
                core.addListener(coreListener)
                coreListenerAdded = true
            }
            dumpSipSetup(core, "at startup")
        }
        refreshToken = securePrefs?.getString(KEY_REFRESH_TOKEN, null)
        if (refreshToken != null) {
            state.postValue(State.RESUMING)
            executor.execute { resumeSession() }
        }
    }

    /** True when a Sufficit session exists (it may still be renewing offline). */
    fun isSignedIn(): Boolean = refreshToken != null || hasFreshAccessToken()

    fun installationId(): String {
        var id = plainPrefs.getString(KEY_INSTALLATION_ID, null)
        if (id.isNullOrEmpty()) {
            id = UUID.randomUUID().toString()
            plainPrefs.edit().putString(KEY_INSTALLATION_ID, id).apply()
        }
        return id
    }

    /**
     * Re-runs registration/polling. Called from the UI thread too, so the
     * state changes synchronously there: a stale READY must never bounce the
     * login screen back to a main screen that has no SIP account.
     */
    fun retry() {
        val next = if (refreshToken != null || hasFreshAccessToken()) State.RESUMING else State.SIGNED_OUT
        if (Looper.myLooper() == Looper.getMainLooper()) state.value = next else state.postValue(next)
        if (next == State.RESUMING) {
            registered = false
            executor.execute { resumeSession() }
        }
    }

    /** Local sign-out: forget the session and stop talking to provisioning. */
    fun signOut() {
        executor.execute {
            loginAttempt++
            stopTimers()
            registered = false
            accessToken = null
            accessTokenExpiresAt = 0
            refreshToken = null
            securePrefs?.edit()?.remove(KEY_REFRESH_TOKEN)?.apply()
            plainPrefs.edit().remove(KEY_APPLIED_VERSION).apply()
            message.postValue(null)
            state.postValue(State.SIGNED_OUT)
        }
    }

    // ---------------------------------------------------------------------
    // Interactive login (system browser + loopback redirect)
    // ---------------------------------------------------------------------

    fun login() {
        val current = state.value
        if (current == State.AUTHORIZING) return
        val attempt = ++loginAttempt
        state.postValue(State.AUTHORIZING)
        message.postValue(null)

        SufficitLoginService.start(appContext)
        Thread({
            try {
                runLogin(attempt)
            } finally {
                // The callback (or its failure) has been handled; the token
                // exchange no longer depends on the browser.
                if (attempt == loginAttempt) SufficitLoginService.stop(appContext)
            }
        }, "SufficitLogin").start()
    }

    private fun runLogin(attempt: Int) {
        val verifier = randomUrlSafe(64)
        val challenge = base64Url(MessageDigest.getInstance("SHA-256").digest(verifier.toByteArray()))
        val expectedState = randomUrlSafe(24)

        val server = try {
            ServerSocket(REDIRECT_PORT, 4, InetAddress.getByName("127.0.0.1"))
        } catch (e: Exception) {
            Log.e("$TAG Callback port $REDIRECT_PORT unavailable: $e")
            fail("A porta local de retorno do login está ocupada. Feche outros apps e tente novamente.")
            return
        }

        try {
            val authorizeUrl = Uri.parse("$AUTHORITY/connect/authorize").buildUpon()
                .appendQueryParameter("client_id", CLIENT_ID)
                .appendQueryParameter("redirect_uri", REDIRECT_URI)
                .appendQueryParameter("response_type", "code")
                .appendQueryParameter("scope", SCOPES)
                .appendQueryParameter("state", expectedState)
                .appendQueryParameter("code_challenge", challenge)
                .appendQueryParameter("code_challenge_method", "S256")
                .build()

            Handler(Looper.getMainLooper()).post {
                try {
                    val intent = Intent(Intent.ACTION_VIEW, authorizeUrl)
                        .addCategory(Intent.CATEGORY_BROWSABLE)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    appContext.startActivity(intent)
                } catch (e: Exception) {
                    Log.e("$TAG Could not open the browser: $e")
                    fail("Nenhum navegador disponível para entrar na conta Sufficit.")
                    try { server.close() } catch (_: Exception) {}
                }
            }

            val callback = waitForCallback(server, attempt) ?: return
            if (attempt != loginAttempt) return

            val error = callback["error"]
            if (error != null) {
                fail(callback["error_description"] ?: "Login recusado ($error).")
                return
            }
            if (callback["state"] != expectedState) {
                Log.e("$TAG State mismatch on the authorization callback")
                fail("Resposta de login inválida. Tente novamente.")
                return
            }
            val code = callback["code"]
            if (code.isNullOrEmpty()) {
                fail("O login não retornou um código de autorização.")
                return
            }

            executor.execute {
                if (attempt != loginAttempt) return@execute
                val ok = tokenRequest(
                    mapOf(
                        "grant_type" to "authorization_code",
                        "code" to code,
                        "redirect_uri" to REDIRECT_URI,
                        "client_id" to CLIENT_ID,
                        "code_verifier" to verifier
                    )
                )
                if (ok == TokenResult.OK) {
                    state.postValue(State.REGISTERING)
                    registerInstallation()
                } else {
                    fail("Não foi possível concluir o login Sufficit. Tente novamente.")
                }
            }
        } finally {
            try { server.close() } catch (_: Exception) {}
        }
    }

    /**
     * Accepts loopback connections until one carries the OAuth response.
     *
     * Each connection is served on its own thread. Browsers open speculative
     * connections to the redirect target before sending the request on one of
     * them; served one at a time, every idle connection held the real callback
     * for its whole read timeout and the browser sat on "returning to the app"
     * (2026-10-25, Chrome 155 opened three).
     */
    private fun waitForCallback(server: ServerSocket, attempt: Int): Map<String, String>? {
        val deadline = System.currentTimeMillis() + LOGIN_TIMEOUT_MS
        val result = AtomicReference<Map<String, String>?>(null)
        val received = CountDownLatch(1)
        val handlers = Executors.newCachedThreadPool()
        try {
            while (attempt == loginAttempt && received.count > 0) {
                val remaining = deadline - System.currentTimeMillis()
                if (remaining <= 0) break
                // Short accept slices so a callback served by a handler thread
                // ends the loop promptly.
                server.soTimeout = remaining.coerceAtMost(1_000L).toInt()
                val socket = try {
                    server.accept()
                } catch (_: SocketTimeoutException) {
                    continue
                } catch (e: Exception) {
                    if (attempt == loginAttempt && received.count > 0) Log.w("$TAG Loopback accept failed: $e")
                    break
                }
                handlers.execute { serveLoopback(socket, result, received) }
            }
        } finally {
            handlers.shutdown()
        }
        result.get()?.let { return it }
        if (attempt == loginAttempt) fail("O login não foi concluído a tempo. Tente novamente.")
        return null
    }

    private fun serveLoopback(
        socket: Socket,
        result: AtomicReference<Map<String, String>?>,
        received: CountDownLatch
    ) {
        socket.use { s ->
            try {
                s.soTimeout = 10_000
                val reader = BufferedReader(InputStreamReader(s.getInputStream(), Charsets.US_ASCII))
                val requestLine = reader.readLine() ?: return
                val target = requestLine.split(" ").getOrNull(1) ?: return
                val uri = Uri.parse("http://127.0.0.1$target")
                val params = HashMap<String, String>()
                for (name in uri.queryParameterNames) {
                    uri.getQueryParameter(name)?.let { params[name] = it }
                }
                val isCallback = params.containsKey("code") || params.containsKey("error")
                val body = if (isCallback) callbackPage() else ""
                val status = if (isCallback) "200 OK" else "404 Not Found"
                val bytes = body.toByteArray(Charsets.UTF_8)
                val out = s.getOutputStream()
                out.write(
                    (
                        "HTTP/1.1 $status\r\nContent-Type: text/html; charset=utf-8\r\n" +
                            "Content-Length: ${bytes.size}\r\nCache-Control: no-store\r\n" +
                            "Connection: close\r\n\r\n"
                        ).toByteArray(Charsets.US_ASCII)
                )
                out.write(bytes)
                out.flush()
                // The page is written before the loop is released, so closing
                // the listener never cuts the browser's response short.
                if (isCallback && result.compareAndSet(null, params)) received.countDown()
            } catch (_: SocketTimeoutException) {
                // An idle speculative connection the browser never used.
            } catch (e: Exception) {
                Log.w("$TAG Ignoring malformed loopback request: $e")
            }
        }
    }

    private fun callbackPage(): String =
        "<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">" +
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" +
            "<title>Sufficit Phone</title></head>" +
            "<body style=\"font-family:sans-serif;text-align:center;padding:48px 16px\">" +
            "<h2>Login concluído</h2><p>Volte ao Sufficit Phone para continuar.</p>" +
            "<p><a href=\"$RETURN_INTENT\" style=\"display:inline-block;padding:12px 24px;" +
            "background:#0b5cad;color:#fff;border-radius:8px;text-decoration:none\">" +
            "Abrir o Sufficit Phone</a></p>" +
            "<p><a href=\"$RETURN_SCHEME://login\">Se o botão não funcionar, toque aqui</a></p>" +
            "<script>setTimeout(function(){location.href='$RETURN_INTENT'},300)</script>" +
            "</body></html>"

    // ---------------------------------------------------------------------
    // Tokens
    // ---------------------------------------------------------------------

    private enum class TokenResult { OK, REJECTED, UNAVAILABLE }

    private fun hasFreshAccessToken(): Boolean =
        accessToken != null && System.currentTimeMillis() < accessTokenExpiresAt - 60_000

    private fun tokenRequest(form: Map<String, String>): TokenResult {
        val body = form.entries.joinToString("&") {
            "${URLEncoder.encode(it.key, "UTF-8")}=${URLEncoder.encode(it.value, "UTF-8")}"
        }
        val response = http("POST", "$AUTHORITY/connect/token", body, "application/x-www-form-urlencoded", false)
            ?: return TokenResult.UNAVAILABLE
        val json = try { JSONObject(response.second) } catch (_: Exception) { JSONObject() }
        if (response.first !in 200..299) {
            val error = json.optString("error")
            Log.w("$TAG Token endpoint answered HTTP ${response.first} [$error]")
            if (error == "invalid_grant" || error == "invalid_client" || error == "unauthorized_client") {
                return TokenResult.REJECTED
            }
            return TokenResult.UNAVAILABLE
        }
        val token = json.optString("access_token")
        if (token.isNullOrEmpty()) return TokenResult.REJECTED
        accessToken = token
        accessTokenExpiresAt = System.currentTimeMillis() + json.optLong("expires_in", 300) * 1000
        // Never log the credential; the shape is enough for diagnostics.
        Log.i("$TAG Access token received (${if (token.count { it == '.' } == 2) "jwt" else "reference"})")
        val rotated = json.optString("refresh_token")
        if (rotated.isNotEmpty()) {
            refreshToken = rotated
            securePrefs?.edit()?.putString(KEY_REFRESH_TOKEN, rotated)?.apply()
        }
        return TokenResult.OK
    }

    /** Makes sure a usable access token exists; false when it cannot be renewed now. */
    private fun ensureAccessToken(force: Boolean = false): Boolean {
        if (!force && hasFreshAccessToken()) return true
        val stored = refreshToken ?: return false
        return when (tokenRequest(mapOf("grant_type" to "refresh_token", "refresh_token" to stored, "client_id" to CLIENT_ID))) {
            TokenResult.OK -> true
            TokenResult.REJECTED -> {
                // Revoked or expired: keeping it would fail the same way forever.
                Log.w("$TAG Refresh token rejected; forgetting the stored session")
                forgetSession()
                false
            }
            TokenResult.UNAVAILABLE -> false
        }
    }

    private fun forgetSession() {
        stopTimers()
        registered = false
        accessToken = null
        accessTokenExpiresAt = 0
        refreshToken = null
        securePrefs?.edit()?.remove(KEY_REFRESH_TOKEN)?.apply()
        message.postValue("Sua sessão Sufficit expirou. Entre novamente.")
        state.postValue(State.SIGNED_OUT)
    }

    private fun resumeSession() {
        if (!ensureAccessToken()) {
            if (refreshToken != null) {
                // Offline or service unavailable: the existing SIP account keeps
                // working; provisioning resumes on the next attempt.
                fail("Sem conexão com a Sufficit no momento. Suas chamadas continuam disponíveis.")
                scheduleHeartbeat()
            }
            return
        }
        state.postValue(State.REGISTERING)
        registerInstallation()
    }

    // ---------------------------------------------------------------------
    // Provisioning service
    // ---------------------------------------------------------------------

    /** Authorized call that renews the access token once on 401. */
    private fun api(method: String, path: String, json: JSONObject? = null): Pair<Int, String>? {
        if (!ensureAccessToken()) return null
        var response = http(method, PROVISIONING_BASE + path, json?.toString(), "application/json", true)
        if (response?.first == 401 && ensureAccessToken(force = true)) {
            response = http(method, PROVISIONING_BASE + path, json?.toString(), "application/json", true)
        }
        return response
    }

    private fun registerInstallation() {
        val response = api("POST", "/installations", JSONObject().put("installationId", installationId()))
        if (response == null || response.first !in 200..299) {
            Log.e("$TAG Installation registration failed: HTTP ${response?.first}")
            if (refreshToken == null && accessToken == null) return
            fail(
                if (response?.first == 403) {
                    "Sua conta Sufficit não tem permissão para provisionar este aparelho."
                } else {
                    "Não foi possível registrar este aparelho na Sufficit. Tente novamente."
                }
            )
            scheduleHeartbeat()
            return
        }
        Log.i("$TAG Installation registered (HTTP ${response.first})")
        registered = true
        scheduleHeartbeat()
        sendHeartbeat()
        pollInstallation()
    }

    private fun pollInstallation() {
        pollTask = null
        if (!registered) return
        val applied = plainPrefs.getLong(KEY_APPLIED_VERSION, -1)
        // Without any SIP account the configuration must be fetched again,
        // even when the server believes this version was already applied.
        val includeApplied = applied >= 0 && hasSipAccount()
        val path = "/installations/${installationId()}" + if (includeApplied) "?appliedVersion=$applied" else ""
        val response = api("GET", path)
        if (response == null || response.first !in 200..299) {
            if (response?.first == 404) {
                Log.w("$TAG Installation no longer known by the server")
                registered = false
                fail("Este aparelho não está mais registrado. Toque em tentar novamente.")
                return
            }
            // Transient: keep trying while the user waits for the ramal.
            if (state.value != State.READY) schedulePoll()
            return
        }
        val json = try { JSONObject(response.second) } catch (_: Exception) { JSONObject() }
        val ramal = json.optString("assignedRamalId").takeUnless { it.isEmpty() || it == "null" }
        if (ramal == null) {
            state.postValue(State.WAITING_FOR_RAMAL)
            schedulePoll()
            return
        }
        val version = json.optLong("configVersion", 0)
        val url = json.optString("provisioningUrl").takeUnless { it.isEmpty() || it == "null" }
        if (url != null) {
            applyConfiguration(PROVISIONING_BASE + url, version)
        } else if (hasSipAccount()) {
            message.postValue(null)
            state.postValue(State.READY)
        } else {
            schedulePoll()
        }
    }

    private fun applyConfiguration(url: String, version: Long) {
        state.postValue(State.APPLYING)
        pendingConfigVersion = version
        coreContext.postOnCoreThread { core ->
            if (!coreListenerAdded) {
                core.addListener(coreListener)
                coreListenerAdded = true
            }
            Log.i("$TAG Applying Sufficit provisioning, restarting the Core")
            core.provisioningUri = url
            core.stop()
            core.start()
        }
    }

    private fun onConfigurationApplied(version: Long) {
        plainPrefs.edit().putLong(KEY_APPLIED_VERSION, version).apply()
        val response = api("POST", "/installations/${installationId()}/applied", JSONObject().put("configVersion", version))
        if (response == null || response.first !in 200..299) {
            // 409 just means a newer version exists; the next check picks it up.
            Log.w("$TAG Could not report applied configuration: HTTP ${response?.first}")
        }
        message.postValue(null)
        state.postValue(State.READY)
    }

    private fun sendHeartbeat() {
        if (!registered) return
        val body = JSONObject()
            .put("clientVersion", BuildConfig.VERSION_NAME)
            .put("clientPlatform", "android")
        val response = api("POST", "/installations/${installationId()}/heartbeat", body)
        if (response == null || response.first !in 200..299) {
            Log.w("$TAG Heartbeat failed: HTTP ${response?.first}")
        }
    }

    private fun heartbeatTick() {
        if (!registered) {
            if (refreshToken != null) resumeSession()
            return
        }
        sendHeartbeat()
        // Cheap when nothing changed: the server only issues a ticket for a
        // configuration version this installation has not applied yet.
        if (pollTask == null && pendingConfigVersion < 0) pollInstallation()
    }

    private fun schedulePoll() {
        if (pollTask == null) {
            pollTask = executor.schedule({ pollInstallation() }, POLL_INTERVAL_MS, TimeUnit.MILLISECONDS)
        }
    }

    private fun scheduleHeartbeat() {
        if (heartbeatTask == null) {
            heartbeatTask = executor.scheduleWithFixedDelay(
                { heartbeatTick() },
                HEARTBEAT_INTERVAL_MS,
                HEARTBEAT_INTERVAL_MS,
                TimeUnit.MILLISECONDS
            )
        }
    }

    private fun stopTimers() {
        pollTask?.cancel(false)
        pollTask = null
        heartbeatTask?.cancel(false)
        heartbeatTask = null
    }

    // ---------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------

    private fun fail(reason: String) {
        SufficitLoginService.stop(appContext)
        message.postValue(reason)
        state.postValue(State.FAILED)
    }

    private fun hasSipAccount(): Boolean {
        val latch = CountDownLatch(1)
        var result = false
        coreContext.postOnCoreThread { core ->
            result = core.accountList.isNotEmpty()
            latch.countDown()
        }
        latch.await(5, TimeUnit.SECONDS)
        return result
    }

    private fun http(method: String, url: String, body: String?, contentType: String, authorized: Boolean): Pair<Int, String>? {
        return try {
            val connection = URL(url).openConnection() as HttpURLConnection
            connection.requestMethod = method
            connection.connectTimeout = 15_000
            connection.readTimeout = 20_000
            connection.setRequestProperty("Accept", "application/json")
            if (authorized) accessToken?.let { connection.setRequestProperty("Authorization", "Bearer $it") }
            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", contentType)
                connection.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
            }
            val code = connection.responseCode
            val stream = if (code in 200..399) connection.inputStream else connection.errorStream
            val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
            connection.disconnect()
            Pair(code, text)
        } catch (e: Exception) {
            Log.w("$TAG $method ${Uri.parse(url).path} failed: $e")
            null
        }
    }

    private fun randomUrlSafe(bytes: Int): String {
        val buffer = ByteArray(bytes)
        SecureRandom().nextBytes(buffer)
        return base64Url(buffer)
    }

    private fun base64Url(data: ByteArray): String =
        Base64.encodeToString(data, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)
}
