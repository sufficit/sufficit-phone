/*
 * Sufficit Phone: availability chosen by the user ("Disponível" or "Não perturbe").
 *
 * The choice becomes the native PBX presence of the assigned extension through the
 * EndPoints presence API (device "pjsip"), so queues and direct calls skip this
 * device (or every device of the extension, when its presence is unified). While
 * "Não perturbe" is on, the app also declines calls that still arrive (internal
 * extension-to-extension calls), like the Chrome extension does.
 * Design: sufficit-pabx-asterisk docs/activities/202611241300-extension-presence.md.
 */
package org.linphone.sufficit

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.SharedPreferences
import android.widget.Toast
import org.linphone.LinphoneApplication.Companion.coreContext
import org.linphone.core.Call
import org.linphone.core.Core
import org.linphone.core.CoreListenerStub
import org.linphone.core.Reason
import org.linphone.core.tools.Log

object SufficitPresence {
    private const val TAG = "[Sufficit Presence]"
    const val AVAILABLE = "available"
    const val DND = "dnd"
    private const val PREFS = "sufficit_presence"
    private const val KEY_STATE = "state"

    private lateinit var prefs: SharedPreferences
    private var listenerAdded = false

    @Volatile private var doNotDisturb = false

    private val coreListener = object : CoreListenerStub() {
        override fun onCallStateChanged(core: Core, call: Call, state: Call.State?, message: String) {
            if (state == Call.State.IncomingReceived && doNotDisturb) {
                Log.i("$TAG Do not disturb is on, declining incoming call")
                call.decline(Reason.Busy)
            }
        }
    }

    fun init(context: Context) {
        prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        doNotDisturb = current() == DND
        if (!listenerAdded) {
            listenerAdded = true
            coreContext.postOnCoreThread { core -> core.addListener(coreListener) }
        }
    }

    fun current(): String = if (::prefs.isInitialized) prefs.getString(KEY_STATE, AVAILABLE) ?: AVAILABLE else AVAILABLE

    private fun label(state: String) = if (state == DND) "Não perturbe" else "Disponível"

    fun menuText(): String = "Disponibilidade: " + label(current())

    private fun save(state: String) {
        prefs.edit().putString(KEY_STATE, state).apply()
        doNotDisturb = state == DND
    }

    /** Shows the choice; [onChanged] receives the new menu text (also when a failure reverts it). */
    fun pick(activity: Activity, onChanged: (String) -> Unit) {
        val states = arrayOf(AVAILABLE, DND)
        val labels = states.map { label(it) }.toTypedArray()
        AlertDialog.Builder(activity)
            .setTitle("Disponibilidade")
            .setSingleChoiceItems(labels, states.indexOf(current()).coerceAtLeast(0)) { dialog, which ->
                dialog.dismiss()
                apply(activity, states[which], onChanged)
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun apply(activity: Activity, state: String, onChanged: (String) -> Unit) {
        val previous = current()
        if (previous == state) return
        save(state)
        onChanged(menuText())
        SufficitAuth.putPresence(state) { code ->
            activity.runOnUiThread {
                val text = when (code) {
                    200 -> "Disponibilidade: " + label(state) + "."
                    401, 403 -> "Para usar a disponibilidade, saia e entre novamente na sua conta Sufficit."
                    -1 -> "Este aparelho ainda não tem um ramal atribuído."
                    else -> "Não foi possível alterar a disponibilidade agora. Tente novamente."
                }
                if (code != 200) {
                    save(previous)
                    onChanged(menuText())
                }
                if (code == 401 || code == 403) {
                    // Sessions from older versions lack the endpoints.phone scope: sign in again,
                    // keeping this installation, its extension and the SIP account.
                    AlertDialog.Builder(activity)
                        .setTitle("Disponibilidade")
                        .setMessage("Para usar a disponibilidade, entre novamente na sua conta Sufficit. Depois escolha a disponibilidade outra vez.")
                        .setPositiveButton("Entrar") { _, _ ->
                            // Opens the browser; its return (sufficitphone://login) brings the login screen back,
                            // which goes on to the main screen once the session is renewed.
                            SufficitAuth.login()
                        }
                        .setNegativeButton("Agora não", null)
                        .show()
                    return@runOnUiThread
                }
                Toast.makeText(activity, text, Toast.LENGTH_LONG).show()
            }
        }
    }
}
