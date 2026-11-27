# Sufficit Phone para iPhone

Base: `linphone-iphone` 6.2.4 (SDK via Swift Package), adaptado por
`scripts/prepare-ios.py` (mesma ideia do Android: parte sempre do código
original e aplica `ios/overlay`).

- Identificador: `com.sufficit.phone`; grupos `group.com.sufficit.phone.*`.
- Nome exibido: Sufficit Phone; ícone B (`branding/icon/sufficit-phone-light.svg`).
- Conta Apple Developer: a do Felipe Almeman (decisão de 30/10/2026).

## Pendências para assinar e distribuir (TestFlight)
Na conta Apple (developer.apple.com / App Store Connect):
1. App ID `com.sufficit.phone` (+ extensões `.linphoneExtension`,
   `.msgNotificationService`, `.intentsExtension`) com Push Notifications,
   App Groups e Associated Domains.
2. App Groups `group.com.sufficit.phone.msgNotification` e
   `group.com.sufficit.phone.linphoneExtension`.
3. Chave da API do App Store Connect (.p8, Key ID, Issuer ID) para o CI.
4. Chave APNs (.p8) para push de chamadas (VoIP/PushKit).
5. Secrets no GitHub: `APPLE_TEAM_ID`, `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_KEY_P8`.

## Já no código (overlay)
- Login Sufficit obrigatório (`ios/overlay/Linphone/SufficitAuth.swift`, `SufficitLoginView.swift`): OAuth PKCE com redirect loopback, refresh token no Keychain, registro da instalação, espera do ramal (botão "Abrir Meu Provisionamento", sem indicador de carregamento), provisionamento de uso único, heartbeat `ios` e NOTIFY `check-sync`.
- O Welcome e "Adicionar conta" levam ao login Sufficit, não ao assistente SIP manual (patches em `scripts/prepare-ios.py`, que aborta se o upstream divergir).

## A fazer no código
Discagem simples (sem sugerir outros servidores/endereços SIP), limite de contas e presença — portar do overlay Android. Validado só por compilação no CI (sem aparelho).
