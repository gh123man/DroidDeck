package com.droiddeck.launcher.glasses

import java.io.IOException

internal class XrealControl(
    private val channels: XrealChannels,
    private val settleDelayMs: Long = 150,
    private val model: XrealModel = XrealModel.ONE_S,
) {
    private var transaction = 0
    private fun exchange(command: Int, value: Int? = null): ByteArray {
        transaction = if (transaction == Int.MAX_VALUE) 1 else transaction + 1
        channels.controlOutput.write(XrealProtocol.request(command, transaction, value))
        channels.controlOutput.flush()
        val deadline = System.nanoTime() + 3_000_000_000L
        repeat(32) { // Bound unsolicited button/status packets and stale responses.
            if (System.nanoTime() > deadline) throw IOException("XREAL control timed out")
            XrealProtocol.response(XrealProtocol.read(channels.controlInput), command, transaction)?.let { return it }
        }
        throw IOException("XREAL control response was not received")
    }
    fun display(): Int = awaitDisplay { true }
    fun changeDisplay(mode: Int): Int {
        require(model.supportsDisplayMode(mode)) { "Unsupported ${model.name} display mode: $mode" }
        val stereo = mode == 5
        set(XrealProtocol.SET_INPUT, if (stereo) 1 else 0)
        // One firmware can restore mode 9 and reject the legacy 2/3/4 EDIDs.
        // Let SET_INPUT restore its native mode; Android selects advertised refresh rates.
        if (model === XrealModel.ONE && !stereo) return awaitDisplay { it != 5 }
        var requested = false
        return awaitDisplay { current ->
            if (current == mode) true else {
                // 1S switches EDID with SET_INPUT; One may require SET_DISPLAY.
                if (!requested && (model === XrealModel.ONE || (current == 5) == stereo)) {
                    val reply = exchange(XrealProtocol.SET_DISPLAY, mode)
                    if (XrealProtocol.acknowledged(reply)) requested = true
                    else if (!XrealProtocol.busy(reply)) commandFailed(XrealProtocol.SET_DISPLAY, reply)
                }
                false
            }
        }
    }
    private fun awaitDisplay(matches: (Int) -> Boolean): Int {
        val deadline = System.nanoTime() + 8_000_000_000L
        repeat(40) {
            val reply = exchange(XrealProtocol.GET_DISPLAY)
            val value = XrealProtocol.numeric(reply)
            if (value != null && matches(value)) return value
            if (value == null && !XrealProtocol.acknowledged(reply))
                throw IOException("Invalid XREAL display response: ${hex(reply)}")
            if (System.nanoTime() > deadline) throw IOException("XREAL display transition timed out")
            Thread.sleep(settleDelayMs)
        }
        throw IOException("XREAL display transition did not complete")
    }
    fun set(command: Int, value: Int) {
        val reply = exchange(command, value)
        if (!XrealProtocol.acknowledged(reply)) commandFailed(command, reply)
    }
    private fun commandFailed(command: Int, reply: ByteArray): Nothing =
        throw IOException("XREAL command %04x was not acknowledged: %s".format(command, hex(reply)))

    private fun hex(reply: ByteArray) = reply.take(32).joinToString("") { "%02x".format(it) }
}
