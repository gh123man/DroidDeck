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
}
