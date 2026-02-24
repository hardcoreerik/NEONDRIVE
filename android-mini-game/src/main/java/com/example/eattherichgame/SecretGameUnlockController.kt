package com.example.eattherichgame

import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.view.View
import kotlin.math.max

class SecretGameUnlockController(
    private val requiredTaps: Int = 5,
    private val windowMs: Long = 5_000L
) {
    private val tapTimes = ArrayDeque<Long>()

    fun onTap(nowMs: Long = SystemClock.elapsedRealtime()): Boolean {
        tapTimes.addLast(nowMs)
        prune(nowMs)
        return tapTimes.size >= requiredTaps
    }

    private fun prune(nowMs: Long) {
        val minAllowed = max(0L, nowMs - windowMs)
        while (tapTimes.isNotEmpty() && tapTimes.first() < minAllowed) {
            tapTimes.removeFirst()
        }
    }
}

fun View.enableSecretGameUnlock(
    context: Context,
    requiredTaps: Int = 5,
    windowMs: Long = 5_000L
) {
    val unlocker = SecretGameUnlockController(requiredTaps, windowMs)
    setOnClickListener {
        if (unlocker.onTap()) {
            context.startActivity(Intent(context, GameActivity::class.java))
        }
    }
}
