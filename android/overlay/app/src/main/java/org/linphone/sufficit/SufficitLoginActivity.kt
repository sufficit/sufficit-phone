/*
 * Sufficit Phone for Android - mandatory Sufficit sign-in screen.
 *
 * Replaces the upstream Linphone assistant (manual SIP / Linphone accounts):
 * the only way to obtain a ramal is signing in with the Sufficit account and
 * letting the provisioning service deliver the configuration.
 */
package org.linphone.sufficit

import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import org.linphone.R
import org.linphone.compatibility.Compatibility
import org.linphone.ui.main.MainActivity

class SufficitLoginActivity : AppCompatActivity() {
    companion object {
        /** Same page the desktop app opens (Meu Provisionamento > Meus dispositivos). */
        private const val RAMAL_ASSIGNMENT_URL = "https://blazor.sufficit.com.br/pages/provisioning/my-devices"
    }

    private lateinit var status: TextView
    private lateinit var detail: TextView
    private lateinit var progress: ProgressBar
    private lateinit var primary: Button
    private lateinit var secondary: Button

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        SufficitAuth.init(applicationContext)

        val density = resources.displayMetrics.density
        fun dp(v: Int) = (v * density).toInt()

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(dp(32), dp(48), dp(32), dp(48))
        }
        root.addView(
            ImageView(this).apply { setImageResource(R.mipmap.ic_launcher) },
            LinearLayout.LayoutParams(dp(96), dp(96))
        )
        root.addView(
            TextView(this).apply {
                text = getString(R.string.app_name)
                textSize = 26f
                setTypeface(typeface, Typeface.BOLD)
                gravity = Gravity.CENTER
                setPadding(0, dp(16), 0, dp(8))
            }
        )
        status = TextView(this).apply {
            textSize = 17f
            gravity = Gravity.CENTER
        }
        root.addView(status)
        detail = TextView(this).apply {
            textSize = 14f
            gravity = Gravity.CENTER
            setPadding(0, dp(8), 0, dp(16))
        }
        root.addView(detail)
        progress = ProgressBar(this)
        root.addView(progress)
        primary = Button(this)
        root.addView(
            primary,
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT)
                .apply { topMargin = dp(24) }
        )
        secondary = Button(this, null, android.R.attr.borderlessButtonStyle)
        root.addView(secondary)
        setContentView(root)

        SufficitAuth.state.observe(this) { render() }
        SufficitAuth.message.observe(this) { render() }

        permissionLauncher.launch(Compatibility.getAllRequiredPermissionsArray())
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        // Return from the browser (sufficitphone://login): nothing to parse,
        // the loopback listener already received the authorization response.
        render()
    }

    private fun render() {
        val state = SufficitAuth.state.value ?: SufficitAuth.State.SIGNED_OUT
        val message = SufficitAuth.message.value
        var busy = true
        var primaryText: String? = null
        var primaryAction: (() -> Unit)? = null
        var secondaryText: String? = null
        var secondaryAction: (() -> Unit)? = null
        val title: String
        var info: String? = message

        when (state) {
            SufficitAuth.State.SIGNED_OUT -> {
                busy = false
                title = "Entre com sua conta Sufficit"
                info = message ?: "Seu ramal é configurado automaticamente após o login."
                primaryText = "Entrar com a Sufficit"
                primaryAction = { SufficitAuth.login() }
            }
            SufficitAuth.State.AUTHORIZING -> {
                title = "Conclua o login no navegador"
                info = "Depois de entrar, volte para o Sufficit Phone."
                secondaryText = "Cancelar"
                secondaryAction = { SufficitAuth.signOut() }
            }
            SufficitAuth.State.RESUMING -> title = "Retomando sua sessão Sufficit…"
            SufficitAuth.State.REGISTERING -> title = "Registrando este aparelho…"
            SufficitAuth.State.WAITING_FOR_RAMAL -> {
                // The next step is the user's (choose a ramal); polling runs silently,
                // so no spinner here: it would wrongly tell the user to wait.
                busy = false
                title = "Escolha o ramal deste aparelho"
                info = "Login concluído. Toque em Escolher ramal, selecione um ramal para este aparelho " +
                    "e volte ao app: a configuração continua automaticamente.\n\n" +
                    "Instalação: " + SufficitAuth.installationId()
                primaryText = "Escolher ramal"
                primaryAction = { openRamalAssignment() }
                secondaryText = "Sair da conta"
                secondaryAction = { SufficitAuth.signOut() }
            }
            SufficitAuth.State.APPLYING -> title = "Configurando seu ramal…"
            SufficitAuth.State.READY -> {
                openMain()
                return
            }
            SufficitAuth.State.FAILED -> {
                busy = false
                title = "Não foi possível concluir"
                primaryText = "Tentar novamente"
                primaryAction = { SufficitAuth.retry() }
                secondaryText = "Sair da conta"
                secondaryAction = { SufficitAuth.signOut() }
            }
        }

        status.text = title
        detail.text = info ?: ""
        detail.visibility = if (info.isNullOrEmpty()) View.GONE else View.VISIBLE
        progress.visibility = if (busy) View.VISIBLE else View.GONE
        primary.visibility = if (primaryText == null) View.GONE else View.VISIBLE
        primary.text = primaryText ?: ""
        primary.setOnClickListener { primaryAction?.invoke() }
        secondary.visibility = if (secondaryText == null) View.GONE else View.VISIBLE
        secondary.text = secondaryText ?: ""
        secondary.setOnClickListener { secondaryAction?.invoke() }
    }

    /** Opens the self-service page where the signed-in user assigns a ramal. */
    private fun openRamalAssignment() {
        try {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(RAMAL_ASSIGNMENT_URL)))
        } catch (e: ActivityNotFoundException) {
            detail.text = "Nenhum navegador disponível. Acesse: $RAMAL_ASSIGNMENT_URL"
            detail.visibility = View.VISIBLE
        }
    }

    private fun openMain() {
        startActivity(Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP))
        finish()
    }

    @Deprecated("Back leaves the app: there is nothing behind a mandatory sign-in.")
    override fun onBackPressed() {
        @Suppress("DEPRECATION")
        super.onBackPressed()
        finishAffinity()
    }
}
