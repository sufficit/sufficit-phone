# Workflows de CI

- `build-linux.yml` — AppImage (ubuntu-24.04, Qt 6.10.3 via aqt).
- `build-windows.yml` — instalador NSIS (windows-2022, VS 2022, Qt 6.10.3 via aqt).
- `build-macos.yml` — DMG (macos-14, Qt 6.10.3 via aqt, não assinado).
- `build-android.yml` — SDK linphone-sdk em AAR único com as 3 ABIs (arm64, armv7, x86_64) via gradle (biblioteca para um futuro app Android).
- `build-ios.yml` — SDK linphone-sdk para simulador (zip + xcframeworks, sem assinatura) (biblioteca para um futuro app iOS).
- `release.yml` — ao empurrar uma tag `X.Y.Z`, monta o Release com os três instaladores.

Nota: este fork contém apenas o aplicativo desktop (Qt/QML). Os jobs Android/iOS
constroem o SDK (linphone-sdk), não aplicativos móveis instaláveis.

## Submódulos offline

`gitlab.linphone.org` fica periodicamente inacessível a partir dos runners
hospedados do GitHub (bloqueio de rede, não falha transitória). Por isso os
cinco workflows de build **não** fazem fetch de submódulos: eles restauram um
snapshot pronto de `external/linphone-sdk` (com todos os submódulos aninhados,
sem os diretórios `.git`) publicado como asset do release `submods-*`.

As variáveis do repositório controlam qual snapshot usar:

- `SUBMODS_BUNDLE_KEY` — hash SHA-256 de `{git ls-tree -r HEAD external/ | awk '$1=="160000"'; cat .gitmodules}`.
  Muda sempre que um submódulo em `external/` avança de commit (ou `.gitmodules` muda).
- `SUBMODS_BUNDLE_SHA256` — hash do tarball publicado (integridade).

Para renovar o bundle depois de atualizar `external/linphone-sdk`:

```bash
KEY=$( { git ls-tree -r HEAD external/ | awk '$1=="160000"'; git show HEAD:.gitmodules; } | sha256sum | cut -d' ' -f1)
tar -C . --exclude='.git' --dereference \
  --exclude='external/linphone-sdk/tester/Android/linphone-sdk-android/linphone-sdk-android-debug.aar' \
  --sort=name --owner=0 --group=0 --numeric-owner \
  -czf "/tmp/submodules-$KEY.tar.gz" external/linphone-sdk
sha256sum "/tmp/submodules-$KEY.tar.gz"
gh variable set SUBMODS_BUNDLE_KEY    --body "$KEY"
gh variable set SUBMODS_BUNDLE_SHA256 --body "<hash acima>"
gh release create "submods-${KEY:0:8}" "/tmp/submodules-$KEY.tar.gz" \
  --title "Submodule bundle ${KEY:0:8}" --notes "snapshot external/linphone-sdk"
```

`--dereference` é obrigatório: symlinks relativos no SDK quebram a extração
no tar do Windows (Git Bash), que não consegue recriá-los; o alvo
`linphone-sdk-android-debug.aar` é um link para artefato de build inexistente
e por isso é excluído.

Os três submódulos em `external/google/` (gn, crashpad, chromium-depot-tools)
continuam vazios no CI: `CMakeLists.txt` só os usa quando o crash handler
(BugSplat/crashpad) está habilitado. No Windows isso exigiria rodar
`gclient sync`/gn, então o workflow do Windows passa `-DENABLE_CRASH_HANDLER=OFF`;
em macOS/Linux o bloco do crashpad é WIN32-only e o alvo nunca é criado.
