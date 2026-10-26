#!/usr/bin/env python3
"""Apply the Sufficit Android build overlay to the pinned, clean upstream checkout."""
from pathlib import Path
import subprocess
import re
import shutil

VERSION_NAME = '0.2.2-pilot'
VERSION_CODE = 4

root = Path(__file__).resolve().parents[1]
app = root / 'build-android-app'
revision = 'dcaa165b77ed374b7beaedc8deae3e8a9b4fd30c'
if not app.exists():
    subprocess.run(['git', 'clone', '--depth', '1', '--branch', '6.2.8',
                    'https://github.com/BelledonneCommunications/linphone-android.git', str(app)], check=True)
actual = subprocess.check_output(['git', '-C', str(app), 'rev-parse', 'HEAD'], text=True).strip()
if actual != revision:
    raise SystemExit('Unexpected upstream revision; refusing to patch')
# Do not reuse upstream Firebase project identifiers for the Sufficit package.
(app / 'app/google-services.json').unlink(missing_ok=True)
v = app / 'gradle/libs.versions.toml'
v.write_text(v.read_text().replace('linphone = "5.5.+"', 'linphone = "5.5.38"'))
p = app / 'app/build.gradle.kts'
s = subprocess.check_output(['git', '-C', str(app), 'show', 'HEAD:app/build.gradle.kts'], text=True).replace('val packageName = "org.linphone"', 'val packageName = "com.sufficit.phone"')
s = s.replace('versionCode = 602008', f'versionCode = {VERSION_CODE}').replace('versionName = "6.2.8"', f'versionName = "{VERSION_NAME}"')
s = s.replace('"linphone-android-${variant.buildType.name}-$gitVersion.apk"', '"sufficit-phone-android-' + VERSION_NAME + '-${variant.buildType.name}.apk"')
p.write_text(s)
for p in (app / 'app/src/main/res').glob('values*/*.xml'):
    s = p.read_text()
    updated = s.replace('<!ENTITY appName "Linphone">', '<!ENTITY appName "Sufficit Phone">')
    if updated != s:
        p.write_text(updated)
messages = {
    'values': 'Sufficit Phone: SIP calls for your extension.',
    'values-pt-rBR': 'Sufficit Phone: chamadas SIP para o seu ramal.',
    'values-fr': 'Sufficit Phone : appels SIP pour votre poste.',
    'values-es': 'Sufficit Phone: llamadas SIP para su extensión.',
}
for locale, message in messages.items():
    p = app / 'app/src/main/res' / locale / 'strings.xml'
    if p.exists():
        p.write_text(re.sub(r'(<string name="welcome_page_1_message">).*?(</string>)',
                            lambda m: m[1] + message + m[2], p.read_text()))

# --- Sufficit sign-in (mandatory) -------------------------------------------
# Sources that do not exist upstream live in android/overlay and are copied
# verbatim; upstream files are patched with exact, verified anchors so a
# changed upstream fails loudly instead of building an APK without login.
overlay = root / 'android/overlay'
for src in overlay.rglob('*'):
    if src.is_file():
        dst = app / src.relative_to(overlay)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def patch(rel, old, new, count=1):
    f = app / rel
    text = f.read_text()
    if new in text:
        return
    if text.count(old) != count:
        raise SystemExit(f'Anchor not found {text.count(old)}x (expected {count}) in {rel}: {old[:60]!r}')
    f.write_text(text.replace(old, new))


java = 'app/src/main/java/org/linphone/'
LOGIN = 'org.linphone.sufficit.SufficitLoginActivity::class.java'

patch('app/src/main/AndroidManifest.xml',
      '        <activity\n            android:name=".ui.welcome.WelcomeActivity"',
      """        <activity
            android:name=".sufficit.SufficitLoginActivity"
            android:exported="true"
            android:launchMode="singleTask"
            android:windowSoftInputMode="adjustResize">
            <intent-filter>
                <action android:name="android.intent.action.VIEW" />
                <category android:name="android.intent.category.DEFAULT" />
                <category android:name="android.intent.category.BROWSABLE" />
                <data android:scheme="sufficitphone" android:host="login" />
            </intent-filter>
        </activity>

        <service
            android:name=".sufficit.SufficitLoginService"
            android:exported="false"
            android:foregroundServiceType="dataSync" />

        <activity
            android:name=".ui.welcome.WelcomeActivity\"""")

patch(java + 'LinphoneApplication.kt',
      '        coreContext.start()\n',
      '        coreContext.start()\n        org.linphone.sufficit.SufficitAuth.init(context)\n')

# Gate: no Sufficit session, no main screen.
patch(java + 'ui/main/MainActivity.kt',
      '        super.onCreate(savedInstanceState)\n\n        binding = DataBindingUtil.setContentView(this, R.layout.main_activity)',
      """        super.onCreate(savedInstanceState)

        org.linphone.sufficit.SufficitAuth.init(applicationContext)
        if (!org.linphone.sufficit.SufficitAuth.isSignedIn()) {
            startActivity(Intent(this, org.linphone.sufficit.SufficitLoginActivity::class.java))
            finish()
            return
        }

        binding = DataBindingUtil.setContentView(this, R.layout.main_activity)""")

# Signed in but no ramal yet: show the provisioning progress, not the SIP assistant.
patch(java + 'ui/main/MainActivity.kt',
      """                Log.w("$TAG No account found, showing Assistant activity")
                coreContext.postOnMainThread {
                    try {
                        startActivity(Intent(this, AssistantActivity::class.java))""",
      """                Log.w("$TAG No account found, showing Sufficit sign-in")
                coreContext.postOnMainThread {
                    try {
                        org.linphone.sufficit.SufficitAuth.retry()
                        startActivity(Intent(this, """ + LOGIN + """))""")

patch(java + 'ui/main/MainActivity.kt',
      """        viewModel.lastAccountRemovedEvent.observe(this) {
            it.consume {
                startActivity(Intent(this, AssistantActivity::class.java))""",
      """        viewModel.lastAccountRemovedEvent.observe(this) {
            it.consume {
                org.linphone.sufficit.SufficitAuth.signOut()
                startActivity(Intent(this, """ + LOGIN + """))
                finish()""")

patch(java + 'ui/main/fragment/DrawerMenuFragment.kt',
      'startActivity(Intent(requireActivity(), AssistantActivity::class.java))',
      'org.linphone.sufficit.SufficitAuth.retry()\n                startActivity(Intent(requireActivity(), ' + LOGIN + '))')

patch(java + 'ui/welcome/WelcomeActivity.kt',
      """        val intent = Intent(this, AssistantActivity::class.java)
        intent.putExtra(AssistantActivity.SKIP_LANDING_EXTRA, true)""",
      """        val intent = Intent(this, """ + LOGIN + """)""")

print('Android overlay prepared at', app)
