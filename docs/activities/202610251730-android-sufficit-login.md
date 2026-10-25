# Login Sufficit obrigatório no APK Android (0.2.0-pilot)

O piloto 0.1.0 abria o assistente upstream do Linphone (conta SIP manual), sem a autenticação Sufficit. A 0.2.0 inclui o mesmo fluxo do desktop (`Linphone/tool/SufficitOAuth.cpp`):

- `android/overlay/.../sufficit/SufficitAuth.kt`: Authorization Code + PKCE S256 pelo navegador do sistema, com redirect loopback `http://127.0.0.1:47623/` do cliente público `sufficit-phone-desktop`. Depois registra a instalação, faz polling do ramal, aplica a URL de provisionamento de uso único, informa `/applied` e envia heartbeat `android` a cada 15 minutos. O refresh token fica só em EncryptedSharedPreferences.
- `SufficitLoginActivity.kt`: tela obrigatória. Sem sessão, a MainActivity redireciona para ela. O Welcome, o menu "adicionar conta" e a falta de conta levam a essa tela, não ao assistente SIP.
- `scripts/prepare-android.py`: copia o overlay e aplica patches por âncora exata; o build aborta se o upstream divergir.

Verificado no Identity em produção: `sufficit-phone-android` não existe (ID2052), e o cliente desktop recusa redirect de esquema customizado (ID2043). Ele aceita o loopback 127.0.0.1:47623, com resposta 302 para o login.

Pendência: um cliente Android próprio no Identity, com redirect de esquema privado, vai exigir a aprovação de um token de gestão temporário.
