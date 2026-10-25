# Instalador Android de piloto

Entregue APK release 0.1.0-pilot, com.sufficit.phone, Android 9+, ARM64/ARMv7, assinado por chave RSA 3072 persistida fora do repositório. Overlay e build reproduzíveis em scripts/prepare-android.py e scripts/build-android.sh; SDK 5.5.38 fixado. Interface upstream Linphone 6.2.8 preservada, nome/pacote/mensagem inicial adaptados. Configuração Firebase upstream removida; nenhuma credencial de ramal incluída.

Validação: assembleRelease concluído; apksigner validou assinatura; aapt confirmou nome, pacote, versão, API mínima e ABIs. Instalação, atualização com mesma assinatura e abertura fria em emulador API 36 passaram. Fluxo chegou à opção de conta SIP de terceiros. Buffer de crash vazio. Emulador isolado encerrado; nenhum aplicativo instalado nos dispositivos físicos. Scripts verificados por bash -n/py_compile e diff --check sem erros.

Publicação: https://github.com/sufficit/sufficit-phone/releases/tag/android-0.1.0-pilot — prerelease pública com APK, fonte correspondente e SHA256SUMS. Download público HTTP 200; digest informado pelo GitHub igual ao local: 893eb17a578f7a744b33275fc2cf4c9f8c68c84f5b88467b2c6ce6a769719c00. APK 89609543 bytes. Chave privada e keystore.properties não incluídos no arquivo de fontes.

Limites: não houve registro SIP nem chamada real no celular da Deborah. Login/provisionamento desktop e push Sufficit não portados; manter o app aberto para o teste. O piloto usa o SDK upstream fixado compatível com a interface, sem substituir os antigos assets SDK do projeto.

[Uso e build](../ANDROID.md) · [Plano](../PLAN-ANDROID-APK.md)
