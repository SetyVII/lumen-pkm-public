package com.sety.lumen.mobile.ui

import android.app.Application
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sety.lumen.mobile.LumenApplication
import com.sety.lumen.mobile.data.JournalDay
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.data.OgPreview
import com.sety.lumen.mobile.data.SyncStatus
import com.sety.lumen.mobile.data.todayIso
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.time.LocalDate
import java.time.YearMonth

data class DayCell(
    val date: LocalDate,
    val inMonth: Boolean,
    val hasContent: Boolean,
    val coverAssetId: String,
)

class LumenViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application as LumenApplication
    val repository = app.repository
    val settings = app.settings
    val vault = app.vault
    val revision: StateFlow<Long> = repository.revision

    var syncRunning by mutableStateOf(false)
        private set
    var syncMessage by mutableStateOf(settings.lastSyncMessage)
        private set
    var syncError by mutableStateOf("")
        private set

    val objectHistory = ObjectHistory()
    var activeObject by mutableStateOf<LumenObject?>(null)
        private set
    var selectedTypeKey by mutableStateOf<String?>(null)

    init {
        viewModelScope.launch {
            repository.revision.collect { refreshActiveObject() }
        }
    }

    fun openObject(obj: LumenObject?) {
        if (obj == null) { closeObject(); return }
        objectHistory.open(obj.id)
        activeObject = obj
    }

    fun closeObject() {
        objectHistory.pop()
        refreshActiveObject()
    }

    fun refreshActiveObject() {
        while (objectHistory.current != null) {
            val obj = repository.getObject(objectHistory.current!!.objectId)
            if (obj != null) { activeObject = obj; return }
            objectHistory.pop()
        }
        activeObject = null
    }

    fun updateContent(id: String, content: String) {
        repository.updateObjectContent(id, content)
        refreshActiveObject()
    }

    fun updateBlocks(id: String, blocks: org.json.JSONArray) {
        repository.updateObjectBlocks(id, blocks)
        refreshActiveObject()
    }

    fun updateProperty(id: String, key: String, value: Any) {
        repository.updateObjectProperty(id, key, value)
        refreshActiveObject()
    }

    fun syncNow() {
        if (syncRunning) return
        viewModelScope.launch {
            syncRunning = true
            syncError = ""
            val outcome = repository.syncNow()
            syncMessage = outcome.message
            if (!outcome.success) syncError = outcome.message
            syncRunning = false
        }
    }

    fun testConnection(onResult: (Result<SyncStatus>) -> Unit) {
        viewModelScope.launch {
            val result = withContext(Dispatchers.IO) { runCatching { repository.testConnection() } }
            onResult(result)
        }
    }

    fun pair(api: String, token: String, fingerprint: String, onResult: (Result<SyncStatus>) -> Unit) {
        viewModelScope.launch {
            val result = withContext(Dispatchers.IO) { runCatching { repository.pair(api, token, fingerprint) } }
            result.onSuccess { syncMessage = "Emparejado con ${it.serverId.take(8)}" }
            onResult(result)
        }
    }

    fun disconnect() {
        repository.disconnect()
        syncMessage = "Desconectado"
    }

    /**
     * Re-enqueues the whole local library (objects and sealed media) and syncs,
     * so a newly paired server receives everything the phone already had.
     */
    fun resendAll(onDone: (Result<Pair<Int, Int>>) -> Unit) {
        viewModelScope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching { repository.enqueueAllForResend() }
            }
            result.onSuccess { (objects, assets) ->
                syncMessage = "Reenvío en cola: $objects objetos y $assets medios"
                syncNow()
            }
            onDone(result)
        }
    }

    fun toggleTask(id: String, done: Boolean) = repository.toggleTask(id, done)
    fun postponeTask(id: String) = repository.postponeTask(id)
    fun deleteObject(id: String) = repository.deleteObject(id)

    fun quickNote(text: String, isTask: Boolean, dueDate: String?, journalDate: String?) {
        repository.quickNote(text, isTask, dueDate, journalDate)
    }

    fun captureMoment(uri: Uri, caption: String, date: String, stripGps: Boolean, onDone: (Result<LumenObject>) -> Unit) {
        viewModelScope.launch {
            val result = runCatching { repository.captureMoment(uri, caption, date, stripGps) }
            result.onSuccess { syncMessage = "Momento guardado en el diario" }
            onDone(result)
        }
    }

    fun saveWebLink(url: String, title: String, preview: OgPreview, imageBytes: ByteArray?, journalDate: String?, onDone: (Result<LumenObject>) -> Unit) {
        viewModelScope.launch {
            val result = runCatching { repository.saveWebLink(url, title, preview, imageBytes, journalDate) }
            onDone(result)
        }
    }

    fun addLink(url: String, title: String, onDone: (Result<LumenObject>) -> Unit) {
        viewModelScope.launch {
            val preview = withContext(Dispatchers.IO) {
                runCatching { com.sety.lumen.mobile.util.OgFetcher.fetch(url) }.getOrDefault(OgPreview())
            }
            val result = runCatching {
                repository.saveWebLink(url, title, preview, preview.imageBytes, null)
            }
            result.onSuccess { syncMessage = "Enlace guardado para revisar" }
            onDone(result)
        }
    }

    fun setWebStatus(id: String, status: String) {
        val obj = repository.db.getObject(id) ?: return
        val props = JSONObject(obj.properties.toString())
        val options = types().firstOrNull { it.id == obj.typeId }?.let { com.sety.lumen.mobile.data.objectFields(it) }
            ?.firstOrNull { it.key == "status" }?.options.orEmpty()
        val canonical = com.sety.lumen.mobile.data.canonicalReviewStatus(status)
        props.put("status", options.firstOrNull { com.sety.lumen.mobile.data.canonicalReviewStatus(it) == canonical } ?: canonical)
        repository.saveObject(obj.copy(properties = props))
    }

    fun pinAsset(id: String, pinned: Boolean) {
        repository.db.setAssetPinned(id, pinned)
        repository.bump()
    }

    fun setReminder(enabled: Boolean, hour: Int) {
        settings.journalReminder = enabled
        settings.reminderHour = hour
        if (enabled) {
            com.sety.lumen.mobile.sync.SyncScheduler.scheduleDaily(app)
        } else {
            com.sety.lumen.mobile.sync.SyncScheduler.cancelDaily(app)
        }
    }

    fun lock() {
        vault.lock()
        app.repository.bump()
    }

    fun wipe() {
        repository.wipe()
    }

    fun objects(typeId: String? = null): List<LumenObject> = repository.objects(typeId)
    fun types() = repository.types()
    fun books() = repository.books()
    fun quotes() = repository.quotes()
    fun images() = repository.images()
    fun getObject(id: String) = repository.getObject(id)
    fun saveBook(
        title: String,
        author: String = "",
        rating: String = "",
        recommendedBy: String = "",
        mediaType: String = "",
        coverUrl: String = "",
        content: String = "",
    ): LumenObject = repository.saveBook(title, author, rating, recommendedBy, mediaType, coverUrl, content)
    fun saveQuote(
        title: String,
        author: String = "",
        source: String = "",
        content: String = "",
    ): LumenObject = repository.saveQuote(title, author, source, content)
    fun journal(date: String) = repository.journal(date)
    fun ensureJournal(date: String) = repository.ensureJournal(date)
    fun search(query: String) = repository.search(query)
    fun tasks() = repository.tasks()
    fun webLinks(status: String? = null) = repository.webLinks(status)
    fun photos(date: String) = repository.photosFor(date)
    fun asset(id: String) = repository.asset(id)
    fun counts() = repository.counts()
    fun journalDays(prefix: String) = repository.journalDays(prefix)

    fun calendar(month: YearMonth): List<DayCell> {
        val first = month.atDay(1)
        val start = first.minusDays((first.dayOfWeek.value - 1).toLong())
        val days = repository.journalDays(month.toString())
        val byDate = days.associateBy(JournalDay::date)
        return (0 until 42).map { offset ->
            val date = start.plusDays(offset.toLong())
            val day = byDate[date.toString()]
            DayCell(date, YearMonth.from(date) == month, day?.hasContent == true, day?.coverAssetId.orEmpty())
        }
    }

    fun today(): String = todayIso()
}
