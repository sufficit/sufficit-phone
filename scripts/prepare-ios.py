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

overlay = ROOT / 'ios' / 'overlay'
for f in overlay.rglob('*'):
    if f.is_file():
        dst = APP / f.relative_to(overlay)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, dst)
print(f'build-ios-app pronto: {BUNDLE} {VERSION} ({BUILD}) team={TEAM or "-"}')
