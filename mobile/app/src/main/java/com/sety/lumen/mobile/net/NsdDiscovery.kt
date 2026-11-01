package com.sety.lumen.mobile.net

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo

class NsdDiscovery(context: Context) {
    private val nsd = context.applicationContext.getSystemService(Context.NSD_SERVICE) as NsdManager
    private var discovery: NsdManager.DiscoveryListener? = null
    private var resolving = false

    fun discover(onFound: (String) -> Unit, onLost: () -> Unit = {}) {
        stop()
        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) = Unit
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) = stop()
            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) = stop()
            override fun onDiscoveryStopped(serviceType: String) = Unit
            override fun onServiceLost(serviceInfo: NsdServiceInfo) = onLost()
            override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                if (resolving) return
                resolving = true
                nsd.resolveService(serviceInfo, object : NsdManager.ResolveListener {
                    override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                        resolving = false
                    }

                    override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                        resolving = false
                        val host = serviceInfo.host?.hostAddress ?: return
                        val tls = serviceInfo.attributes?.get("tls")?.toString(Charsets.UTF_8) == "1"
                        onFound("${if (tls) "https" else "http"}://$host:${serviceInfo.port}")
                    }
                })
            }
        }
        discovery = listener
        runCatching { nsd.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener) }
    }

    fun stop() {
        discovery?.let { runCatching { nsd.stopServiceDiscovery(it) } }
        discovery = null
        resolving = false
    }

    companion object {
        const val SERVICE_TYPE = "_lumen-sync._tcp."
    }
}

class WifiWatcher(context: Context, private val onHomeWifi: () -> Unit) {
    private val manager = context.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private val callback = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) {
            val caps = manager.getNetworkCapabilities(network) ?: return
            if (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) &&
                caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
            ) {
                onHomeWifi()
            }
        }
    }

    fun start() {
        runCatching { manager.registerDefaultNetworkCallback(callback) }
    }

    fun stop() {
        runCatching { manager.unregisterNetworkCallback(callback) }
    }
}
