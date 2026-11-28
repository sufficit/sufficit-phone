/*
 * Sufficit Phone para iPhone - disponibilidade escolhida pelo usuario ("Disponivel" ou "Nao perturbe").
 *
 * Port de SufficitPresence.kt (Android). A escolha vira a presenca nativa do ramal no PBX (API de
 * presenca do EndPoints, dispositivo "pjsip"): filas e chamadas diretas pulam este aparelho.
 * Com "Nao perturbe" ligado o app tambem recusa as chamadas que ainda chegarem (ramal para ramal).
 */
import Foundation
import SwiftUI
import linphonesw

final class SufficitPresence: ObservableObject {
	static let shared = SufficitPresence()

	static let available = "available"
	static let dnd = "dnd"
	private let key = "sufficit_presence_state"

	struct Notice: Identifiable {
		let id = UUID()
		let message: String
		let needsLogin: Bool
	}

	@Published private(set) var state: String
	@Published var notice: Notice? = nil
	private var delegate: CoreDelegateStub?
	private var doNotDisturb: Bool

	private init() {
		let saved = UserDefaults.standard.string(forKey: "sufficit_presence_state") ?? SufficitPresence.available
		state = saved
		doNotDisturb = saved == SufficitPresence.dnd
	}

	/// Chamado uma vez, junto com os demais delegates do Core (fila do worker do SufficitAuth).
	func install() {
		if delegate != nil { return }
		let d = CoreDelegateStub(onCallStateChanged: { (_: Core, call: Call, cstate: Call.State, _: String) in
			if cstate == .IncomingReceived && self.doNotDisturb {
				Log.info("[Sufficit Presence] Do not disturb is on, declining incoming call")
				try? call.decline(reason: .Busy)
			}
		})
		delegate = d
		CoreContext.shared.addCoreDelegateStub(delegate: d)
	}

	private func label(_ s: String) -> String { s == SufficitPresence.dnd ? "Não perturbe" : "Disponível" }

	var menuText: String { "Disponibilidade: " + label(state) }

	private func save(_ s: String) {
		UserDefaults.standard.set(s, forKey: key)
		doNotDisturb = s == SufficitPresence.dnd
		state = s
	}

	func choose(_ s: String) {
		let previous = state
		if previous == s { return }
		save(s)
		SufficitAuth.shared.putPresence(s) { code in
			switch code {
			case 200:
				self.notice = Notice(message: "Disponibilidade: " + self.label(s) + ".", needsLogin: false)
			case 401, 403:
				// Sessoes de versoes antigas nao tem o escopo endpoints.phone: entrar de novo, mantendo
				// a instalacao, o ramal e a conta SIP.
				self.save(previous)
				self.notice = Notice(message: "Para usar a disponibilidade, entre novamente na sua conta Sufficit. Depois escolha a disponibilidade outra vez.", needsLogin: true)
			case -1:
				self.save(previous)
				self.notice = Notice(message: "Este aparelho ainda não tem um ramal atribuído.", needsLogin: false)
			default:
				self.save(previous)
				self.notice = Notice(message: "Não foi possível alterar a disponibilidade agora. Tente novamente.", needsLogin: false)
			}
		}
	}
}

/// Entrada do menu lateral, acima de "Configuracoes".
struct SufficitPresenceEntry: View {
	@ObservedObject private var presence = SufficitPresence.shared
	@State private var showPicker = false

	var body: some View {
		SideMenuEntry(
			iconName: presence.state == SufficitPresence.dnd ? "bell-slash" : "bell",
			title: presence.menuText
		)
		.onTapGesture { showPicker = true }
		.confirmationDialog("Disponibilidade", isPresented: $showPicker, titleVisibility: .visible) {
			Button("Disponível") { presence.choose(SufficitPresence.available) }
			Button("Não perturbe") { presence.choose(SufficitPresence.dnd) }
			Button("Cancelar", role: .cancel) {}
		}
		.alert(item: $presence.notice) { n in
			if n.needsLogin {
				return Alert(
					title: Text("Disponibilidade"),
					message: Text(n.message),
					primaryButton: .default(Text("Entrar")) { SufficitAuth.shared.login() },
					secondaryButton: .cancel(Text("Agora não"))
				)
			}
			return Alert(title: Text("Disponibilidade"), message: Text(n.message), dismissButton: .default(Text("OK")))
		}
	}
}
