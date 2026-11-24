#!/usr/bin/env python3
"""Apply the Sufficit Android build overlay to the pinned, clean upstream checkout."""
from pathlib import Path
import subprocess
import re
import shutil

VERSION_NAME = '0.2.8-pilot'
VERSION_CODE = 10

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

# Play: Wi-Fi is optional (mobile data works); avoid implicit required feature
patch('app/src/main/AndroidManifest.xml',
      '''    <uses-feature
        android:name="android.hardware.camera"
        android:required="false" />
''',
      '''    <uses-feature
        android:name="android.hardware.camera"
        android:required="false" />
    <uses-feature
        android:name="android.hardware.wifi"
        android:required="false" />
''')

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

# Sufficit: extra accounts. The provisioned account is always the first one;
# "Adicionar uma conta" opens the SIP account form directly (other servers,
# including third parties). Enabled by default; provisioning disables it with
# [ui] max_account=1 (upstream key, 0 = unlimited) whenever we want.
patch(java + 'ui/main/fragment/DrawerMenuFragment.kt',
      'startActivity(Intent(requireActivity(), AssistantActivity::class.java))',
      'startActivity(Intent(requireActivity(), AssistantActivity::class.java).putExtra(AssistantActivity.SUFFICIT_EXTRA_ACCOUNT, true))')
patch(java + 'ui/assistant/AssistantActivity.kt',
      '        const val SKIP_LANDING_EXTRA = "SkipLandingIfAtLeastAnAccount"',
      '        const val SKIP_LANDING_EXTRA = "SkipLandingIfAtLeastAnAccount"\n        const val SUFFICIT_EXTRA_ACCOUNT = "SufficitExtraAccount"')
patch(java + 'ui/assistant/AssistantActivity.kt',
      '            } else if (intent.getBooleanExtra(SKIP_LANDING_EXTRA, false)) {',
      '''            } else if (intent.getBooleanExtra(SUFFICIT_EXTRA_ACCOUNT, false)) {
                binding.assistantNavContainer.findNavController().navigate(R.id.thirdPartySipAccountLoginFragment)
            } else if (intent.getBooleanExtra(SKIP_LANDING_EXTRA, false)) {''')
patch(java + 'ui/assistant/fragment/ThirdPartySipAccountLoginFragment.kt',
      '''    private fun goBack() {
        findNavController().popBackStack()''',
      '''    private fun goBack() {
        if (requireActivity().intent.getBooleanExtra(org.linphone.ui.assistant.AssistantActivity.SUFFICIT_EXTRA_ACCOUNT, false)) {
            requireActivity().finish()
            return
        }
        findNavController().popBackStack()''')

patch(java + 'ui/welcome/WelcomeActivity.kt',
      """        val intent = Intent(this, AssistantActivity::class.java)
        intent.putExtra(AssistantActivity.SKIP_LANDING_EXTRA, true)""",
      """        val intent = Intent(this, """ + LOGIN + """)""")

# Sufficit: simple dialer. Users dial a ramal/number; the server is always the
# provisioned account. Never show SIP URIs/servers nor linphone.org directory.
patch(java + 'core/CorePreferences.kt',
      'get() = config.getBool("ui", "hide_sip_addresses", false)',
      'get() = true // Sufficit: never show SIP servers to the user')
patch(java + 'core/CorePreferences.kt',
      'get() = config.getBool("ui", "only_display_sip_uri_username", false)',
      'get() = true // Sufficit: show only the ramal/number')
patch(java + 'core/CorePreferences.kt',
      'get() = config.getString("ui", "contacts_filter", "")!! // Default value must be empty!',
      'get() = "*" // Sufficit: any SIP domain (no linphone.org directory filter)')

# Sufficit: screenshots/screen recording allowed by default (support, store
# screenshots). Provisioning can still enable it with ui.enable_secure_mode=1.
patch(java + 'core/CorePreferences.kt',
      'get() = config.getBool("ui", "enable_secure_mode", true)',
      'get() = config.getBool("ui", "enable_secure_mode", false) // Sufficit: off by default')

# Sufficit: availability ("Disponível" / "Não perturbe") in the side menu, above the
# settings entry. It becomes the PBX presence of the extension (SufficitPresence.kt).
patch('app/src/main/res/layout/drawer_menu.xml',
      """                app:layout_constraintBottom_toTopOf="@id/settings" />

            <androidx.appcompat.widget.AppCompatTextView
                android:onClick="@{settingsClickedListener}\"""",
      """                app:layout_constraintBottom_toTopOf="@id/sufficit_presence" />

            <androidx.appcompat.widget.AppCompatTextView
                style="@style/header_style"
                android:id="@+id/sufficit_presence"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_marginBottom="10dp"
                android:layout_marginStart="10dp"
                android:layout_marginEnd="10dp"
                android:padding="5dp"
                android:text="Disponibilidade"
                android:drawableStart="@drawable/bell_slash"
                android:drawableEnd="@drawable/caret_right"
                android:drawablePadding="8dp"
                android:background="@drawable/squircle_transparent_button_background"
                app:drawableTint="?attr/color_main2_500"
                app:layout_constraintStart_toStartOf="parent"
                app:layout_constraintEnd_toEndOf="parent"
                app:layout_constraintBottom_toTopOf="@id/settings" />

            <androidx.appcompat.widget.AppCompatTextView
                android:onClick="@{settingsClickedListener}\"""")
patch(java + 'ui/main/fragment/DrawerMenuFragment.kt',
      """        binding.setHelpClickedListener {""",
      """        binding.sufficitPresence.text = org.linphone.sufficit.SufficitPresence.menuText()
        binding.sufficitPresence.setOnClickListener {
            org.linphone.sufficit.SufficitPresence.pick(requireActivity()) { text ->
                binding.sufficitPresence.text = text
            }
        }

        binding.setHelpClickedListener {""")

print('Android overlay prepared at', app)
