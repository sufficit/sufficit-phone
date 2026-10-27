# APK Android do Sufficit Phone

Objetivo: gerar instalador Android assinado para teste de ramal, sem credenciais embutidas.

1. Concluído: identificar aplicativo Android upstream compatível, SDK e ferramentas locais.
2. Concluído: integrar aplicativo e identidade Sufficit, compilar APK instalável.
3. Concluído: verificar pacote, assinatura e abertura quando houver dispositivo de teste disponível; entregar artefato e registrar limitações.

Preservar desktop e SDK existentes. Não substituir aplicativos no telefone da Deborah sem acesso/identificação do dispositivo. Plano de supervisão permanece no Fleet, aguardando teste operacional.

Base fixada: Linphone Android 6.2.8, commit dcaa165b77ed374b7beaedc8deae3e8a9b4fd30c. Aplicativo separado com com.sufficit.phone, Android 9+, configuração SIP manual. Interface upstream preservada nesta primeira versão; provisionamento desktop não é copiado.

Compilação release concluída, assinatura RSA 3072 validada, pacote com.sufficit.phone/API mínimo 28/ARM64+ARMv7 conferido por aapt. APK instalado no emulador isolado emulator-5580 (API 36 com tradução ARM64).

Entregue em https://github.com/sufficit/sufficit-phone/releases/tag/android-0.1.0-pilot. Download HTTP 200 e digest remoto conferido. Registro SIP/chamada no aparelho final não testados. [Registro da entrega](activities/202610251308-android-pilot-apk.md).

## Teste autorizado no celular conectado

1. Concluído: app 0.2.1-pilot registrado, chamada real originada; caiu na rota SIP antiga.
2. Concluído: substituído SIPCHANINFO(peername) por CHANNEL(peername), com backup e reload. Chamada real reconhecida como ramal 6001 e mídia Fleet READY.
3. Concluído: conversa conv_3301m5tnnh9sf2tr6t6a09c7vtht reconheceu fala real e produziu resposta; chamada encerrada pelo canal exato.
4. Concluído: 18 cenários de regressão aprovados; chamada repetida conectou em ~0,51 s e foi encerrada. Evidências registradas no PABX; configuração do aparelho preservada. Sussurro pelo painel não coberto por esse teste.

Testado na 0.2.1-pilot (versionCode 3) instalada no aparelho.
