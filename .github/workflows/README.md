# Workflows de CI

- `build-linux.yml` — AppImage (ubuntu-24.04, Qt 6.10.3 via aqt).
- `build-windows.yml` — instalador NSIS (windows-2022, VS 2022, Qt 6.10.3 via aqt).
- `build-macos.yml` — DMG (macos-14, Qt 6.10.3 via aqt, não assinado).
- `build-android.yml` — SDK linphone-sdk em AAR por ABI (biblioteca para um futuro app Android).
- `build-ios.yml` — SDK linphone-sdk xcframework para simulador (biblioteca para um futuro app iOS).
- `release.yml` — ao empurrar uma tag `X.Y.Z`, monta o Release com os três instaladores.

Nota: este fork contém apenas o aplicativo desktop (Qt/QML). Os jobs Android/iOS
constroem o SDK (linphone-sdk), não aplicativos móveis instaláveis.
