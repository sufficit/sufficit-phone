/*
 * Sufficit Phone para iPhone - tela obrigatoria de entrada com a conta Sufficit.
 * Substitui o assistente SIP manual do Linphone. Na espera do ramal nao ha indicador de
 * carregamento: quem age e o usuario; a verificacao continua em segundo plano.
 */
import SwiftUI

struct SufficitLoginView: View {
	@ObservedObject private var auth = SufficitAuth.shared

	var body: some View {
		VStack(spacing: 20) {
			Spacer()
			Image("linphone")
				.resizable()
				.renderingMode(.template)
				.aspectRatio(contentMode: .fit)
				.frame(width: 160, height: 90)
				.foregroundColor(Color.orangeMain500)
			Text("Sufficit Phone").font(.system(size: 26, weight: .bold))

			switch auth.phase {
			case .waitingForRamal:
				Text("Escolha o ramal deste aparelho")
					.font(.system(size: 20, weight: .semibold))
				Text("Abra o Meu Provisionamento, escolha o ramal deste iPhone e volte aqui. A configuracao e aplicada sozinha.")
					.multilineTextAlignment(.center)
				actionButton("Abrir Meu Provisionamento") { SufficitAuth.openMyDevices() }
			case .failed:
				Text(auth.message ?? "Algo deu errado.")
					.multilineTextAlignment(.center)
				if auth.signedIn {
					actionButton("Tentar novamente") { auth.retry() }
				} else {
					actionButton("Entrar com a conta Sufficit") { auth.login() }
				}
			case .authorizing, .resuming, .registering, .applying:
				ProgressView()
				Text(busyText)
			default:
				Text("Entre com a sua conta Sufficit para usar o ramal da sua empresa.")
					.multilineTextAlignment(.center)
				actionButton("Entrar com a conta Sufficit") { auth.login() }
			}
			Spacer()
		}
		.padding(.horizontal, 32)
		.frame(maxWidth: .infinity, maxHeight: .infinity)
		.background(Color.white.ignoresSafeArea())
		.onAppear { SufficitAuth.shared.start() }
	}

	private var busyText: String {
		switch auth.phase {
		case .authorizing: return "Aguardando o login..."
		case .applying: return "Configurando o ramal..."
		default: return "Conectando..."
		}
	}

	private func actionButton(_ title: String, action: @escaping () -> Void) -> some View {
		Button(action: action) {
			Text(title)
				.font(.system(size: 17, weight: .semibold))
				.foregroundColor(.white)
				.frame(maxWidth: .infinity)
				.padding(.vertical, 14)
				.background(Color.orangeMain500)
				.cornerRadius(60)
		}
	}
}
