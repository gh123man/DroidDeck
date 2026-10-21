package com.droiddeck.tools

import android.content.Context
import android.net.ConnectivityManager
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import java.io.DataInputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Read-only app_process probe: compares sensor timestamps with TCP arrival timing. */
object ReportTiming {
    @JvmStatic fun main(args: Array<String>) {
        if (args.size == 2 && args[0] == "--pose") {
            capturePose(args[1])
            return
        }
        Looper.prepareMainLooper()
        val activityThread = Class.forName("android.app.ActivityThread")
        val thread = activityThread.getMethod("systemMain").invoke(null)
        val context = activityThread.getMethod("getSystemContext").invoke(thread) as Context
        val manager = context.getSystemService(ConnectivityManager::class.java)
        val address = InetAddress.getByName("169.254.2.1")
        val network = manager.allNetworks.first { network ->
            manager.getLinkProperties(network)?.routes?.any {
                !it.hasGateway() && it.destination.prefixLength > 0 && it.matches(address)
            } == true
        }
        Socket().use { socket ->
            network.bindSocket(socket)
            socket.soTimeout = 2000
            socket.tcpNoDelay = true
            socket.connect(InetSocketAddress(address, 52998), 1500)
            ParcelFileDescriptor.fromSocket(socket).use { descriptor ->
                val input = DataInputStream(socket.getInputStream())
                val quickAck = "--quickack" in args
                val gaps = mutableListOf<Double>()
                val age = mutableListOf<Long>()
                var previous = 0L
                var queued = 0
                val start = System.nanoTime()
                while (System.nanoTime() - start < 5_000_000_000L) {
                    // Linux TCP_QUICKACK is transient and must be rearmed after reads.
                    if (quickAck) Os.setsockoptInt(descriptor.fileDescriptor, OsConstants.IPPROTO_TCP, 12, 1)
                    val command = input.readUnsignedShort()
                    val size = input.readInt()
                    require(size in 0..65536)
                    val body = ByteArray(size)
                    input.readFully(body)
                    val now = System.nanoTime()
                    if (command != 0x2836 || size != 128) continue
                    val bytes = ByteBuffer.wrap(body).order(ByteOrder.LITTLE_ENDIAN)
                    if (bytes.getInt(24) != 11) continue
                    if (previous != 0L) gaps.add((now - previous) / 1e6)
                    previous = now
                    age.add(now - bytes.getLong(8))
                    queued = maxOf(queued, input.available())
                }
                gaps.sort()
                val offsets = age.sorted()
                fun percentile(values: List<Double>, q: Double) = values[((values.size - 1) * q).toInt()]
                val relativeAge = offsets.map { (it - offsets.first()) / 1e6 }
                println("quickAck=$quickAck samples=${age.size} maxQueuedBytes=$queued")
                println("arrival gap ms p50=${percentile(gaps,.5)} p95=${percentile(gaps,.95)} p99=${percentile(gaps,.99)} max=${gaps.last()}")
                println("relative sensor age ms p50=${percentile(relativeAge,.5)} p95=${percentile(relativeAge,.95)} p99=${percentile(relativeAge,.99)} max=${relativeAge.last()}")
                println("gapsOver10ms=${gaps.count { it > 10 }} gapsOver25ms=${gaps.count { it > 25 }}")
            }
        }
    }

    private fun capturePose(path: String) {
        java.io.RandomAccessFile(path, "r").use { file ->
            val map = file.channel.map(java.nio.channels.FileChannel.MapMode.READ_ONLY, 0, 128)
                .order(ByteOrder.LITTLE_ENDIAN)
            require(map.getInt(0) == 0x50484444 && map.getInt(4) == 1)
            val rows = mutableListOf<String>()
            var previous = 0
            val start = System.nanoTime()
            while (System.nanoTime() - start < 10_000_000_000L) {
                val seq = map.getInt(8)
                if (seq and 1 == 0 && seq != previous) {
                    val flags = map.getInt(12)
                    val time = map.getLong(56)
                    val matrix = FloatArray(9) { map.getFloat(16 + it * 4) }
                    val now = System.nanoTime()
                    if (map.getInt(8) == seq) {
                        rows.add("$now,$time,$seq,$flags," + matrix.joinToString(","))
                        previous = seq
                    }
                }
                java.util.concurrent.locks.LockSupport.parkNanos(500_000)
            }
            println("observed_ns,published_ns,seq,flags,m00,m01,m02,m10,m11,m12,m20,m21,m22")
            rows.forEach(::println)
        }
    }

}
