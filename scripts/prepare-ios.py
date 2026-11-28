#!/usr/bin/env python3
"""Prepara build-ios-app/: linphone-iphone 6.2.4 com a identidade Sufficit Phone.

Igual ao prepare-android.py: parte sempre do código original (git checkout),
troca identificador, grupos de app, nome exibido, versão e ícone, e aplica
ios/overlay por cima. Assinatura (Team ID) vem de SUFFICIT_IOS_TEAM_ID.
"""
import os, pathlib, shutil, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
APP = ROOT / 'build-ios-app'
TAG = '6.2.4'
VERSION = os.environ.get('SUFFICIT_IOS_VERSION', '0.1.0')
BUILD = os.environ.get('SUFFICIT_IOS_BUILD', '1')
TEAM = os.environ.get('SUFFICIT_IOS_TEAM_ID', '')
BUNDLE = 'com.sufficit.phone'

if not APP.exists():
    subprocess.run(['git', 'clone', '--depth', '1', '--branch', TAG,
                    'https://github.com/BelledonneCommunications/linphone-iphone.git', str(APP)], check=True)
subprocess.run(['git', '-C', str(APP), 'checkout', '--', '.'], check=True)
subprocess.run(['git', '-C', str(APP), 'clean', '-fdq'], check=True)

def edit(rel, pairs):
    p = APP / rel
    s = p.read_text()
    for a, b in pairs:
        s = s.replace(a, b)
    p.write_text(s)

ids = [('group.org.linphone.phone', 'group.' + BUNDLE),
       ('org.linphone.phone', BUNDLE)]
for rel in ['Shared.xcconfig', 'Linphone/Linphone.entitlements', 'Linphone/Info.plist',
            'linphoneExtension/linphoneExtension.entitlements',
            'msgNotificationService/msgNotificationService.entitlements']:
    edit(rel, ids)

pbx = 'LinphoneApp.xcodeproj/project.pbxproj'
edit(pbx, ids + [
    ('INFOPLIST_KEY_CFBundleDisplayName = Linphone;', 'INFOPLIST_KEY_CFBundleDisplayName = "Sufficit Phone";'),
    ('MARKETING_VERSION = 6.2.4;', f'MARKETING_VERSION = {VERSION};'),
    ('CURRENT_PROJECT_VERSION = 2;', f'CURRENT_PROJECT_VERSION = {BUILD};'),
    ('DEVELOPMENT_TEAM = Z2V957B3D6;', f'DEVELOPMENT_TEAM = {TEAM};' if TEAM else 'DEVELOPMENT_TEAM = "";'),
])

# --- Login Sufficit obrigatorio -------------------------------------------------------------
def patch_once(rel, old, new):
    f = APP / rel
    t = f.read_text()
    if old not in t:
        raise SystemExit(f'ancora nao encontrada em {rel}: {old[:60]!r} (upstream mudou?)')
    f.write_text(t.replace(old, new, 1))

# 1) Arquivos novos entram no alvo Linphone (o grupo nao e sincronizado), ao lado do SplashScreen.swift.
def add_sources(names):
    t = (APP / pbx).read_text()
    anchor_build = 'D7A03FC62ACC458A0081A588 /* SplashScreen.swift in Sources */ = {isa = PBXBuildFile; fileRef = D7A03FC52ACC458A0081A588 /* SplashScreen.swift */; };\n'
    anchor_ref = 'D7A03FC52ACC458A0081A588 /* SplashScreen.swift */ = {isa = PBXFileReference; fileEncoding = 4; lastKnownFileType = sourcecode.swift; path = SplashScreen.swift; sourceTree = "<group>"; };\n'
    anchor_group = '\t\t\t\tD7A03FC52ACC458A0081A588 /* SplashScreen.swift */,\n'
    anchor_phase = '\t\t\t\tD7A03FC62ACC458A0081A588 /* SplashScreen.swift in Sources */,\n'
    for a in (anchor_build, anchor_ref, anchor_group, anchor_phase):
        if t.count(a) != 1:
            raise SystemExit('ancora do pbxproj nao encontrada ou duplicada: ' + a[:70])
    for i, n in enumerate(names):
        ref, bf = f'5F1C0A{i:02d}0000000000000001', f'5F1C0A{i:02d}0000000000000002'
        t = t.replace(anchor_build, anchor_build + f'\t\t{bf} /* {n} in Sources */ = {{isa = PBXBuildFile; fileRef = {ref} /* {n} */; }};\n', 1)
        t = t.replace(anchor_ref, anchor_ref + f'\t\t{ref} /* {n} */ = {{isa = PBXFileReference; fileEncoding = 4; lastKnownFileType = sourcecode.swift; path = {n}; sourceTree = "<group>"; }};\n', 1)
        t = t.replace(anchor_group, anchor_group + f'\t\t\t\t{ref} /* {n} */,\n', 1)
        t = t.replace(anchor_phase, anchor_phase + f'\t\t\t\t{bf} /* {n} in Sources */,\n', 1)
    (APP / pbx).write_text(t)

add_sources(['SufficitAuth.swift', 'SufficitLoginView.swift', 'SufficitPresence.swift'])

# 2) Sem conta: tela de login Sufficit em vez do assistente SIP manual.
patch_once('Linphone/LinphoneApp.swift',
           '\t\t\t\t\t\tAssistantView()\n',
           '\t\t\t\t\t\tSufficitLoginView()\n')
# O Welcome upstream leva ao assistente SIP: pula direto para o login.
patch_once('Linphone/LinphoneApp.swift',
           '\t\t!sharedMainViewModel.welcomeViewDisplayed\n',
           '\t\tfalse\n')
# Sessao Sufficit retomada ja na abertura, mesmo com conta SIP configurada.
patch_once('Linphone/LinphoneApp.swift',
           '\tfunc application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {\n',
           '\tfunc application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {\n\t\tSufficitAuth.shared.start()\n')
# 3) Menu lateral: "Adicionar uma conta" leva ao login Sufficit (nunca ao SIP manual).
patch_once('Linphone/UI/Main/Fragments/SideMenu.swift',
           '\t\t\t\t\t\t\tisShowLoginFragment = true\n',
           '\t\t\t\t\t\t\tSufficitAuth.shared.login()\n')

# 4) Limite de contas: [ui] max_account (0 = sem limite) vem do provisionamento. Com o limite atingido
#    o "Adicionar uma conta" some do menu (a chave existe no upstream, mas nada a usava).
patch_once('Linphone/UI/Main/Fragments/SideMenu.swift',
           '\t\t\t\t\tHStack {\n\t\t\t\t\t\tImage("plus-circle")\n',
           '\t\t\t\t\tif AppServices.corePreferences.maxAccountsCount == 0 || CoreContext.shared.accounts.count < AppServices.corePreferences.maxAccountsCount {\n'
           '\t\t\t\t\tHStack {\n\t\t\t\t\t\tImage("plus-circle")\n')
patch_once('Linphone/UI/Main/Fragments/SideMenu.swift',
           '\t\t\t\t\t\t\tSufficitAuth.shared.login()\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n',
           '\t\t\t\t\t\t\tSufficitAuth.shared.login()\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n\t\t\t\t\t}\n')

# 5) Discagem simples: liga sempre pelo ramal provisionado; nunca mostra servidores/enderecos SIP
#    nem o diretorio linphone.org.
patch_once('Linphone/Core/CorePreferences.swift',
           '\t\tget { _hideSipAddresses }\n',
           '\t\tget { true } // Sufficit: nunca mostrar servidores SIP ao usuario\n')
patch_once('Linphone/Core/CorePreferences.swift',
           '\t\t\tlet raw = config.getString(section: "ui", key: "contacts_filter", defaultString: "")\n\t\t\treturn safeString(raw, defaultValue: "")\n',
           '\t\t\t_ = config // Sufficit: qualquer dominio SIP (sem filtro do diretorio linphone.org)\n\t\t\treturn "*"\n')

# 6) Disponibilidade no menu lateral, acima de Configuracoes.
patch_once('Linphone/UI/Main/Fragments/SideMenu.swift',
           '\t\t\t\t\t\tSideMenuEntry(\n\t\t\t\t\t\t\ticonName: "gear",\n',
           '\t\t\t\t\t\tSufficitPresenceEntry()\n\t\t\t\t\t\t\n\t\t\t\t\t\tSideMenuEntry(\n\t\t\t\t\t\t\ticonName: "gear",\n')

overlay = ROOT / 'ios' / 'overlay'
for f in overlay.rglob('*'):
    if f.is_file():
        dst = APP / f.relative_to(overlay)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, dst)
print(f'build-ios-app pronto: {BUNDLE} {VERSION} ({BUILD}) team={TEAM or "-"}')
