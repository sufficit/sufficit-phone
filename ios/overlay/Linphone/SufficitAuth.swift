/*
 * Sufficit Phone para iPhone - login Sufficit e provisionamento da instalacao.
 *
 * Port de SufficitAuth.kt (Android) / SufficitOAuth.cpp (desktop):
 *  - Authorization Code + PKCE S256 contra identity.sufficit.com.br. O cliente publico
 *    "sufficit-phone-desktop" so aceita o redirect loopback http://127.0.0.1:47623/ (RFC 8252):
 *    um listener local recebe o codigo enquanto a folha do navegador esta aberta e a fecha depois.
 *  - O refresh token fica so no Keychain (This Device Only), sem copia em texto puro.
 *  - A instalacao se registra, aguarda a atribuicao do ramal, aplica a URL de provisionamento de
 *    uso unico, informa /applied e envia heartbeat "ios" a cada 15 minutos.
 *  - NOTIFY "check-sync" do PBX so acorda a verificacao: nada da configuracao vem nele.
 */
import Foundation
import SwiftUI
import AuthenticationServices
import CryptoKit
import Network
import Security
import UIKit
import linphonesw

final class SufficitAuth: NSObject, ObservableObject, ASWebAuthenticationPresentationContextProviding {
	static let shared = SufficitAuth()

	enum Phase { case signedOut, authorizing, resuming, registering, waitingForRamal, applying, ready, failed }

	@Published var phase: Phase = .signedOut
	@Published var message: String? = nil
	@Published var signedIn: Bool = false

	private let authority = "https://identity.sufficit.com.br"
	private let clientId = "sufficit-phone-desktop"
	private let redirectPort: UInt16 = 47623
	private let redirectUri = "http://127.0.0.1:47623/"
	private let scopes = "openid profile offline_access provisioning.installation endpoints.phone"
	private let provisioningBase = "https://provisioning.sufficit.com.br:26511"
	static let myDevicesUrl = "https://blazor.sufficit.com.br/pages/provisioning/my-devices"

	private let pollInterval: TimeInterval = 3
	private let heartbeatInterval: TimeInterval = 15 * 60
	private let loginTimeout: TimeInterval = 5 * 60
	private let checkSyncMinInterval: TimeInterval = 10

	// Todo o estado mutavel abaixo e tocado so na fila `worker`.
	private let worker = DispatchQueue(label: "com.sufficit.phone.auth")
	private var started = false
	private var accessToken: String?
	private var accessTokenExpiresAt = Date.distantPast
	private var refreshToken: String?
	private var registered = false
	private var pollScheduled = false
	private var heartbeatGeneration = 0
	private var heartbeatRunning = false
	private var loginAttempt = 0
	private var currentPhase: Phase = .signedOut
	private var pendingConfigVersion: Int64 = -1
	private var lastCheckSync = Date.distantPast
	private var listener: NWListener?
	private var webSession: ASWebAuthenticationSession?
	private var configDelegate: CoreDelegateStub?
	private var notifyDelegate: CoreDelegateStub?

	private let defaults = UserDefaults.standard
	private let kInstallation = "sufficit_installation_id"
	private let kApplied = "sufficit_applied_config_version"
	private let kRamal = "sufficit_assigned_ramal_id"

	// MARK: - Ciclo de vida

	func start() {
		worker.async {
			if self.started { return }
			self.started = true
			self.refreshToken = Keychain.read("refresh_token")
			let has = self.refreshToken != nil
			DispatchQueue.main.async { self.signedIn = has }
			self.installCoreDelegates()
			if has {
				self.setPhase(.resuming)
				self.resumeSession()
			}
		}
	}

	func installationId() -> String {
		if let id = defaults.string(forKey: kInstallation), !id.isEmpty { return id }
		let id = UUID().uuidString.lowercased()
		defaults.set(id, forKey: kInstallation)
		return id
	}

	func retry() {
		worker.async {
			if self.refreshToken != nil {
				self.registered = false
				self.setPhase(.resuming)
				self.resumeSession()
			} else {
				self.setPhase(.signedOut)
			}
		}
	}

	func signOut() {
		worker.async {
			self.loginAttempt += 1
			self.heartbeatGeneration += 1
			self.heartbeatRunning = false
			self.registered = false
			self.accessToken = nil
			self.accessTokenExpiresAt = .distantPast
			self.refreshToken = nil
			Keychain.delete("refresh_token")
			self.defaults.removeObject(forKey: self.kApplied)
			DispatchQueue.main.async { self.signedIn = false }
			self.setPhase(.signedOut)
		}
	}

	private func setPhase(_ p: Phase, message m: String? = nil) {
		currentPhase = p
		DispatchQueue.main.async {
			self.phase = p
			self.message = m
		}
	}

	private func fail(_ reason: String) {
		Log.error("[Sufficit Auth] \(reason)")
		setPhase(.failed, message: reason)
	}

	// MARK: - Login interativo

	func login() {
		worker.async {
			if self.currentPhase == .authorizing { return }
			self.loginAttempt += 1
			self.setPhase(.authorizing)
			self.runLogin(attempt: self.loginAttempt)
		}
	}

	private func runLogin(attempt: Int) {
		let verifier = randomUrlSafe(64)
		let challenge = base64Url(Data(SHA256.hash(data: Data(verifier.utf8))))
		let expectedState = randomUrlSafe(24)

		var comps = URLComponents(string: "\(authority)/connect/authorize")!
		comps.queryItems = [
			URLQueryItem(name: "client_id", value: clientId),
			URLQueryItem(name: "redirect_uri", value: redirectUri),
			URLQueryItem(name: "response_type", value: "code"),
			URLQueryItem(name: "scope", value: scopes),
			URLQueryItem(name: "state", value: expectedState),
			URLQueryItem(name: "code_challenge", value: challenge),
			URLQueryItem(name: "code_challenge_method", value: "S256")
		]
		guard let authorizeUrl = comps.url else { fail("Nao foi possivel iniciar o login."); return }

		var callbackReceived = false
		do {
			let params = NWParameters.tcp
			params.requiredLocalEndpoint = NWEndpoint.hostPort(host: "127.0.0.1", port: NWEndpoint.Port(rawValue: redirectPort)!)
			params.allowLocalEndpointReuse = true
			let l = try NWListener(using: params)
			l.newConnectionHandler = { [weak self] connection in
				self?.serveLoopback(connection) { query in
					self?.worker.async {
						guard let self = self, !callbackReceived, attempt == self.loginAttempt else { return }
						callbackReceived = true
						self.stopLoginUi()
						self.handleCallback(query, verifier: verifier, expectedState: expectedState)
					}
				}
			}
			l.start(queue: worker)
			listener = l
		} catch {
			fail("A porta local de retorno do login esta ocupada. Feche outros apps e tente novamente.")
			return
		}

		DispatchQueue.main.async {
			let session = ASWebAuthenticationSession(url: authorizeUrl, callbackURLScheme: "sufficitphone") { [weak self] _, error in
				// Fechar a folha antes do retorno do navegador cancela o login.
				self?.worker.async {
					guard let self = self, attempt == self.loginAttempt, !callbackReceived, error != nil else { return }
					self.stopLoginUi()
					self.setPhase(.signedOut)
				}
			}
			session.presentationContextProvider = self
			session.prefersEphemeralWebBrowserSession = false
			self.worker.async { self.webSession = session }
			if !session.start() {
				self.worker.async {
					self.stopLoginUi()
					self.fail("Nao foi possivel abrir o navegador para entrar na conta Sufficit.")
				}
			}
		}

		worker.asyncAfter(deadline: .now() + loginTimeout) { [weak self] in
			guard let self = self, attempt == self.loginAttempt, self.currentPhase == .authorizing, !callbackReceived else { return }
			self.stopLoginUi()
			self.fail("O login demorou demais. Tente novamente.")
		}
	}

	private func stopLoginUi() {
		listener?.cancel()
		listener = nil
		let s = webSession
		webSession = nil
		DispatchQueue.main.async { s?.cancel() }
	}

	private func serveLoopback(_ connection: NWConnection, onCallback: @escaping ([String: String]) -> Void) {
		connection.start(queue: worker)
		connection.receive(minimumIncompleteLength: 1, maximumLength: 8192) { data, _, _, _ in
			guard let data = data, let text = String(data: data, encoding: .utf8),
				  let line = text.components(separatedBy: "\r\n").first else {
				connection.cancel()
				return
			}
			let parts = line.split(separator: " ")
			var query: [String: String] = [:]
			if parts.count > 1, let url = URLComponents(string: "http://127.0.0.1" + String(parts[1])) {
				for item in url.queryItems ?? [] { query[item.name] = item.value ?? "" }
			}
			let isCallback = query["code"] != nil || query["error"] != nil
			let html = isCallback
				? "<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Sufficit Phone</title></head><body style=\"font-family:sans-serif;text-align:center;padding:48px 16px\"><h2>Login concluido</h2><p>Voltando ao Sufficit Phone...</p></body></html>"
				: ""
			let body = Data(html.utf8)
			let head = "HTTP/1.1 \(isCallback ? "200 OK" : "404 Not Found")\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: \(body.count)\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"
			connection.send(content: Data(head.utf8) + body, completion: .contentProcessed { _ in
				connection.cancel()
				if isCallback { onCallback(query) }
			})
		}
	}

	private func handleCallback(_ query: [String: String], verifier: String, expectedState: String) {
		if let error = query["error"] {
			fail(query["error_description"] ?? "Login recusado (\(error)).")
			return
		}
		guard query["state"] == expectedState else { fail("Resposta de login invalida. Tente novamente."); return }
		guard let code = query["code"], !code.isEmpty else { fail("O login nao retornou um codigo de autorizacao."); return }
		let result = tokenRequest([
			"grant_type": "authorization_code", "code": code, "redirect_uri": redirectUri,
			"client_id": clientId, "code_verifier": verifier
		])
		if result == .ok {
			DispatchQueue.main.async { self.signedIn = true }
			setPhase(.registering)
			registerInstallation()
		} else {
			fail("Nao foi possivel concluir o login Sufficit. Tente novamente.")
		}
	}

	func presentationAnchor(for session: ASWebAuthenticationSession) -> ASPresentationAnchor {
		let scene = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first
		return scene?.windows.first(where: { $0.isKeyWindow }) ?? scene?.windows.first ?? ASPresentationAnchor()
	}

	// MARK: - Tokens

	private enum TokenResult { case ok, rejected, unavailable }

	private func hasFreshAccessToken() -> Bool {
		accessToken != nil && Date() < accessTokenExpiresAt.addingTimeInterval(-60)
	}

	private func tokenRequest(_ form: [String: String]) -> TokenResult {
		let allowed = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~")
		let body = form.map { k, v in
			"\(k)=\(v.addingPercentEncoding(withAllowedCharacters: allowed) ?? "")"
		}.joined(separator: "&")
		guard let response = http("POST", "\(authority)/connect/token", body: body,
								  contentType: "application/x-www-form-urlencoded", authorized: false) else { return .unavailable }
		guard (200...299).contains(response.status),
			  let json = try? JSONSerialization.jsonObject(with: Data(response.body.utf8)) as? [String: Any],
			  let token = json["access_token"] as? String else {
			return response.status >= 500 ? .unavailable : .rejected
		}
		accessToken = token
		let expires = (json["expires_in"] as? Double) ?? 300
		accessTokenExpiresAt = Date().addingTimeInterval(expires)
		if let refresh = json["refresh_token"] as? String, !refresh.isEmpty {
			refreshToken = refresh
			Keychain.write("refresh_token", refresh)
		}
		return .ok
	}

	private func ensureAccessToken(force: Bool = false) -> Bool {
		if !force && hasFreshAccessToken() { return true }
		guard let refresh = refreshToken else { return false }
		switch tokenRequest(["grant_type": "refresh_token", "refresh_token": refresh, "client_id": clientId]) {
		case .ok: return true
		case .rejected:
			// Sessao revogada ou expirada: exige entrar de novo.
			refreshToken = nil
			accessToken = nil
			Keychain.delete("refresh_token")
			DispatchQueue.main.async { self.signedIn = false }
			setPhase(.signedOut)
			return false
		case .unavailable: return false
		}
	}

	private func resumeSession() {
		if !ensureAccessToken() {
			if refreshToken != nil {
				fail("Sem conexao com a Sufficit no momento. Suas chamadas continuam disponiveis.")
				scheduleHeartbeat()
			}
			return
		}
		setPhase(.registering)
		registerInstallation()
	}

	// MARK: - Servico de provisionamento

	private func api(_ method: String, _ path: String, json: [String: Any]? = nil) -> (status: Int, body: String)? {
		guard ensureAccessToken() else { return nil }
		let data = json.flatMap { try? JSONSerialization.data(withJSONObject: $0) }
		let text = data.flatMap { String(data: $0, encoding: .utf8) }
		var r = http(method, provisioningBase + path, body: text, contentType: "application/json", authorized: true)
		if r?.status == 401 && ensureAccessToken(force: true) {
			r = http(method, provisioningBase + path, body: text, contentType: "application/json", authorized: true)
		}
		return r
	}

	private func registerInstallation() {
		guard let r = api("POST", "/installations", json: ["installationId": installationId()]),
			  (200...299).contains(r.status) else {
			if refreshToken == nil && accessToken == nil { return }
			fail("Nao foi possivel registrar este aparelho na Sufficit. Tente novamente.")
			scheduleHeartbeat()
			return
		}
		registered = true
		scheduleHeartbeat()
		sendHeartbeat()
		pollInstallation()
	}

	private func pollInstallation() {
		pollScheduled = false
		guard registered else { return }
		CoreContext.shared.doOnCoreQueue { core in
			let count = core.accountList.count
			self.worker.async { self.pollInstallation(accounts: count) }
		}
	}

	private func pollInstallation(accounts: Int) {
		guard registered else { return }
		let applied = defaults.object(forKey: kApplied) as? Int64 ?? -1
		// Sem nenhuma conta SIP a configuracao precisa ser buscada de novo.
		let includeApplied = applied >= 0 && accounts > 0
		let path = "/installations/\(installationId())" + (includeApplied ? "?appliedVersion=\(applied)" : "")
		let response = api("GET", path)
		guard let r = response, (200...299).contains(r.status) else {
			if response?.status == 404 {
				registered = false
				fail("Este aparelho nao esta mais registrado. Toque em tentar novamente.")
				return
			}
			if currentPhase != .ready { schedulePoll() }
			return
		}
		let json = (try? JSONSerialization.jsonObject(with: Data(r.body.utf8)) as? [String: Any]) ?? [:]
		let ramal = (json["assignedRamalId"] as? String).flatMap { $0.isEmpty ? nil : $0 }
		if let ramal = ramal { defaults.set(ramal, forKey: kRamal) }
		guard ramal != nil else {
			setPhase(.waitingForRamal)
			schedulePoll()
			return
		}
		let version = (json["configVersion"] as? NSNumber)?.int64Value ?? 0
		if let url = (json["provisioningUrl"] as? String).flatMap({ $0.isEmpty ? nil : $0 }) {
			applyConfiguration(provisioningBase + url, version: version)
		} else if accounts > 0 {
			setPhase(.ready)
		} else {
			schedulePoll()
		}
	}

	private func applyConfiguration(_ url: String, version: Int64) {
		CoreContext.shared.doOnCoreQueue { core in
			// Aplicar reinicia o Core: nunca durante uma chamada.
			if core.callsNb > 0 {
				self.worker.async { self.schedulePoll() }
				return
			}
			self.worker.async {
				self.pendingConfigVersion = version
				self.setPhase(.applying)
			}
			Log.info("[Sufficit Auth] Applying Sufficit provisioning, restarting the Core")
			try? core.setProvisioninguri(newValue: url)
			core.stop()
			try? core.start()
		}
	}

	private func installCoreDelegates() {
		let config = CoreDelegateStub(onConfiguringStatus: { (core: Core, status: ConfiguringState, message: String) in
			self.worker.async {
				let version = self.pendingConfigVersion
				if version < 0 { return }
				if status == .Successful || status == .Failed {
					// A URL carrega um ticket de uso unico: deixar configurada faria cada start falhar.
					try? core.setProvisioninguri(newValue: "")
					self.pendingConfigVersion = -1
				}
				if status == .Successful {
					self.onConfigurationApplied(version)
				} else if status == .Failed {
					self.fail("Nao foi possivel aplicar a configuracao do ramal. Tente novamente.")
				}
			}
		})
		configDelegate = config
		CoreContext.shared.addCoreDelegateStub(delegate: config)

		let notify = CoreDelegateStub(onNotifyReceived: { (_: Core, _: Event, notifiedEvent: String, _: Content?) in
			// PBX: NOTIFY "check-sync" so acorda a verificacao; a API autenticada decide.
			let name = notifiedEvent.split(separator: ";").first.map { $0.trimmingCharacters(in: .whitespaces) } ?? ""
			guard name.caseInsensitiveCompare("check-sync") == .orderedSame else { return }
			self.worker.async { self.checkSyncRequested() }
		})
		notifyDelegate = notify
		CoreContext.shared.addCoreDelegateStub(delegate: notify)
	}

	private func onConfigurationApplied(_ version: Int64) {
		defaults.set(version, forKey: kApplied)
		if let r = api("POST", "/installations/\(installationId())/applied", json: ["configVersion": version]),
		   !(200...299).contains(r.status) {
			// 409 so indica que existe versao mais nova; a proxima verificacao a pega.
			Log.warn("[Sufficit Auth] Could not report applied configuration: HTTP \(r.status)")
		}
		setPhase(.ready)
	}

	private func checkSyncRequested() {
		let now = Date()
		if now.timeIntervalSince(lastCheckSync) < checkSyncMinInterval { return }
		lastCheckSync = now
		if !registered {
			if refreshToken != nil { resumeSession() }
			return
		}
		if !pollScheduled && pendingConfigVersion < 0 { pollInstallation() }
	}

	private func sendHeartbeat() {
		guard registered else { return }
		let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
		_ = api("POST", "/installations/\(installationId())/heartbeat",
				json: ["clientVersion": version, "clientPlatform": "ios"])
	}

	private func schedulePoll() {
		if pollScheduled { return }
		pollScheduled = true
		worker.asyncAfter(deadline: .now() + pollInterval) { self.pollInstallation() }
	}

	private func scheduleHeartbeat() {
		if heartbeatRunning { return }
		heartbeatRunning = true
		heartbeatGeneration += 1
		heartbeatLoop(heartbeatGeneration)
	}

	private func heartbeatLoop(_ generation: Int) {
		worker.asyncAfter(deadline: .now() + heartbeatInterval) {
			guard generation == self.heartbeatGeneration else { return }
			if !self.registered {
				if self.refreshToken != nil { self.resumeSession() }
			} else {
				self.sendHeartbeat()
				if !self.pollScheduled && self.pendingConfigVersion < 0 { self.pollInstallation() }
			}
			self.heartbeatLoop(generation)
		}
	}

	// MARK: - Auxiliares

	private func http(_ method: String, _ url: String, body: String?, contentType: String, authorized: Bool) -> (status: Int, body: String)? {
		guard let u = URL(string: url) else { return nil }
		var req = URLRequest(url: u, timeoutInterval: 20)
		req.httpMethod = method
		req.setValue("application/json", forHTTPHeaderField: "Accept")
		if authorized, let t = accessToken { req.setValue("Bearer \(t)", forHTTPHeaderField: "Authorization") }
		if let body = body {
			req.httpBody = Data(body.utf8)
			req.setValue(contentType, forHTTPHeaderField: "Content-Type")
		}
		let sem = DispatchSemaphore(value: 0)
		var out: (Int, String)?
		URLSession.shared.dataTask(with: req) { data, resp, error in
			if let http = resp as? HTTPURLResponse {
				out = (http.statusCode, data.flatMap { String(data: $0, encoding: .utf8) } ?? "")
			} else if let error = error {
				Log.warn("[Sufficit Auth] \(method) \(u.path) failed: \(error.localizedDescription)")
			}
			sem.signal()
		}.resume()
		_ = sem.wait(timeout: .now() + 30)
		return out.map { (status: $0.0, body: $0.1) }
	}

	private func randomUrlSafe(_ bytes: Int) -> String {
		var buffer = [UInt8](repeating: 0, count: bytes)
		_ = SecRandomCopyBytes(kSecRandomDefault, bytes, &buffer)
		return base64Url(Data(buffer))
	}

	private func base64Url(_ data: Data) -> String {
		data.base64EncodedString()
			.replacingOccurrences(of: "+", with: "-")
			.replacingOccurrences(of: "/", with: "_")
			.replacingOccurrences(of: "=", with: "")
	}

	static func openMyDevices() {
		if let url = URL(string: myDevicesUrl) { UIApplication.shared.open(url) }
	}
}

/// Keychain so neste aparelho (fora de backup e de outros iPhones).
enum Keychain {
	private static let service = "com.sufficit.phone.auth"

	private static func query(_ key: String) -> [String: Any] {
		[kSecClass as String: kSecClassGenericPassword,
		 kSecAttrService as String: service,
		 kSecAttrAccount as String: key]
	}

	static func read(_ key: String) -> String? {
		var q = query(key)
		q[kSecReturnData as String] = true
		q[kSecMatchLimit as String] = kSecMatchLimitOne
		var item: CFTypeRef?
		guard SecItemCopyMatching(q as CFDictionary, &item) == errSecSuccess, let data = item as? Data else { return nil }
		return String(data: data, encoding: .utf8)
	}

	static func write(_ key: String, _ value: String) {
		delete(key)
		var q = query(key)
		q[kSecValueData as String] = Data(value.utf8)
		q[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
		SecItemAdd(q as CFDictionary, nil)
	}

	static func delete(_ key: String) {
		SecItemDelete(query(key) as CFDictionary)
	}
}
