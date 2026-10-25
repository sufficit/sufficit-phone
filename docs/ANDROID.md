# Sufficit Phone para Android — piloto

APK separado do aplicativo desktop, com identificador `com.sufficit.phone`, nome Sufficit Phone e versão `0.2.0-pilot` (versionCode 2). Android 9/API 28 ou superior; ARMv7 e ARM64. Preserva a interface e os recursos SIP do Linphone Android. Não contém contas ou senhas de ramais.

## Login Sufficit (obrigatório)

A partir da 0.2.0 o app só abre após login com a conta Sufficit; o assistente de conta SIP manual/Linphone foi substituído pela tela `SufficitLoginActivity`. Fluxo idêntico ao desktop (`Linphone/tool/SufficitOAuth.cpp`):

1. Authorization Code + PKCE S256 em `identity.sufficit.com.br`, pelo navegador do sistema, com redirect loopback `http://127.0.0.1:47623/` (RFC 8252 §7.3) do cliente público `sufficit-phone-desktop` já registrado; nenhum segredo no APK.
2. `POST /installations` com o token Bearer; polling até um ramal ser atribuído; a URL de provisionamento de uso único é aplicada via `core.provisioningUri` + reinício do Core e removida em seguida; `POST /applied` informa a versão.
3. Refresh token apenas em `EncryptedSharedPreferences` (Android Keystore), sem fallback em texto puro; sessão retomada silenciosamente ao abrir o app. Heartbeat (`clientPlatform=android`) a cada 15 minutos verifica nova configuração.
4. `invalid_grant` apaga a sessão e volta ao login; remover a última conta SIP também encerra a sessão.

Código novo em `android/overlay/` (copiado no build); os arquivos upstream são alterados por âncoras exatas em `scripts/prepare-android.py`, que aborta se o upstream mudar.

Pendência no Identity: registrar um cliente próprio (`sufficit-phone-android`) com redirect de esquema privado; hoje `sufficit-phone-android` não existe (ID2052) e o cliente desktop recusa redirect customizado (ID2043).

## Fontes e compilação

Base: [Linphone Android 6.2.8](https://github.com/BelledonneCommunications/linphone-android/tree/dcaa165b77ed374b7beaedc8deae3e8a9b4fd30c), revisão fixada `dcaa165b77ed374b7beaedc8deae3e8a9b4fd30c`. SDK Maven `org.linphone:linphone-sdk-android:5.5.38`, compatível com essa interface. O SDK desktop/Android alpha anteriormente publicado pelo repositório não foi substituído. Fontes do SDK: https://gitlab.linphone.org/BC/public/linphone-sdk/-/tree/5.5.38. Licenças e atribuições upstream preservadas.

Com JDK 21 e Android SDK (plataforma 37.0, build-tools 36.0.0/37.0.0) instalados:

```sh
export JAVA_HOME=/caminho/do/jdk-21
export ANDROID_HOME=/caminho/do/android-sdk
./scripts/build-android.sh
```

O script prepara o upstream em `build-android-app`, aplica nome/pacote/versão e fixa o SDK. A configuração Firebase upstream é removida; não pertence à Sufficit. O APK assinado e SHA256SUMS ficam em `dist-android/`.

A chave de piloto e senha ficam em `$HOME/.config/sufficit/phone-android-signing`, fora dos fontes, com permissões restritas. Preservar essa chave para instalar atualizações sobre o mesmo pacote; não usar uma chave diferente silenciosamente. Não é assinatura da Play Store. O arquivo temporário keystore.properties do checkout de build não deve entrar no pacote de fontes ou no Git.

## Uso no teste de atendimento

Instalar o APK, tocar em **Entrar com a Sufficit**, concluir o login no navegador e voltar ao app. O ramal precisa estar atribuído à instalação (ID exibido na tela de espera) pela gestão do provisionamento. Após registrar, discar 6099. Para supervisão, usar outro ramal e o botão Sussurrar da chamada exata no painel, atendendo o callback.

Nesta versão, manter o aplicativo aberto para o teste: push Firebase da Sufficit não foi configurado. Instalação/abertura não comprovam registro SIP nem qualidade da chamada no telefone final.

[Plano e validação](PLAN-ANDROID-APK.md).

## Download

[APK de piloto 0.1.0](https://github.com/sufficit/sufficit-phone/releases/download/android-0.1.0-pilot/sufficit-phone-android-0.1.0-pilot-release.apk). [Fontes, hashes e notas](https://github.com/sufficit/sufficit-phone/releases/tag/android-0.1.0-pilot).
