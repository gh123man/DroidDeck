package com.droiddeck.launcher.glasses

internal object MotionDiagnostics {
    private val available = try {
        System.loadLibrary("droiddeckwayland")
        true
    } catch (_: UnsatisfiedLinkError) {
        false
    }

    fun sample(sample: XrealProtocol.Sample, receivedNs: Long, pose: FloatArray?, reset: Boolean) {
        if (available) nativeSample(sample.timeNs, receivedNs, pose, reset,
            sample.gyro.x, sample.gyro.y, sample.gyro.z, sample.accel.x, sample.accel.y, sample.accel.z)
    }

    fun clear() { if (available) nativeClear() }
    fun recenter() { if (available) nativeRecenter() }

    private external fun nativeSample(sensorNs: Long, receivedNs: Long, pose: FloatArray?, reset: Boolean,
        gx: Float, gy: Float, gz: Float, ax: Float, ay: Float, az: Float)
    private external fun nativeClear()
    private external fun nativeRecenter()
}
