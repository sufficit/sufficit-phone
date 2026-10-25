/*
 * Sufficit Phone for Android - keeps the process alive during the sign-in.
 *
 * The OAuth response arrives on a loopback socket served by this process while
 * the browser is in front. Some vendors (Samsung "Freecess") freeze background
 * apps about a minute after they leave the screen; a frozen process cannot
 * answer the browser and the login hangs on "returning to the app"
 * (2026-10-25, Galaxy A51). A short-lived foreground service exempts the app
 * from that freeze only while a login is pending.
 */
package org.linphone.sufficit

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import org.linphone.R
import org.linphone.core.tools.Log

class SufficitLoginService : Service() {
    companion object {
        private const val TAG = "[Sufficit Login Service]"
        private const val CHANNEL_ID = "sufficit_login"
        private const val NOTIFICATION_ID = 47623

        fun start(context: Context) {
            try {
                val intent = Intent(context, SufficitLoginService::class.java)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    context.startForegroundService(intent)
                } else {
                    context.startService(intent)
                }
            } catch (e: Exception) {
                // Never block the login: without the service it still works on
                // devices that do not freeze background apps.
                Log.w("$TAG Could not start: $e")
            }
        }

        fun stop(context: Context) {
            try {
                context.stopService(Intent(context, SufficitLoginService::class.java))
            } catch (e: Exception) {
                Log.w("$TAG Could not stop: $e")
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "Login Sufficit", NotificationManager.IMPORTANCE_LOW)
            )
        }
        val open = PendingIntent.getActivity(
            this,
            0,
            Intent(this, SufficitLoginActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val notification: Notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.linphone_notification)
            .setContentTitle("Entrando na conta Sufficit\u2026")
            .setContentText("Conclua o login no navegador.")
            .setContentIntent(open)
            .setOngoing(true)
            .setSilent(true)
            .build()
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
            Log.i("$TAG Keeping the app active during the sign-in")
        } catch (e: Exception) {
            Log.w("$TAG startForeground refused: $e")
            stopSelf()
        }
        return START_NOT_STICKY
    }

    override fun onTimeout(startId: Int, fgsType: Int) {
        stopSelf()
    }
}
