package com.sety.lumen.mobile.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.ime
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Scaffold
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sety.lumen.mobile.CaptureMomentActivity
import com.sety.lumen.mobile.QuickNoteActivity
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.data.todayIso
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppRoot(onLock: () -> Unit) {
    val vm: LumenViewModel = viewModel()
    androidx.compose.runtime.CompositionLocalProvider(LocalDocumentMediaImporter provides { uri -> vm.repository.importFile(uri) }) {
        Box(Modifier.fillMaxSize().imePadding()) { AppRootContent(onLock) }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AppRootContent(onLock: () -> Unit) {
    val vm: LumenViewModel = viewModel()
    val revision by vm.revision.collectAsStateWithLifecycle()
    var currentTab by rememberSaveable { mutableStateOf(LumenNavTab.Home) }
    var selectedDate by rememberSaveable { mutableStateOf(todayIso()) }
    var showCalendar by remember { mutableStateOf(false) }
    var showSettings by remember { mutableStateOf(false) }
    var showCreateSheet by remember { mutableStateOf(false) }
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val scope = rememberCoroutineScope()
    val context = LocalContext.current

    val activeObj = vm.activeObject

    BackHandler(
        enabled = activeObj != null || showCalendar || showSettings || vm.selectedTypeKey != null
    ) {
        when {
            activeObj != null -> vm.closeObject()
            showCalendar -> showCalendar = false
            showSettings -> showSettings = false
            vm.selectedTypeKey != null -> vm.selectedTypeKey = null
        }
    }

    // Launchers for CaptureMomentActivity & QuickNoteActivity
    val captureLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        vm.repository.bump()
    }
    val quickNoteLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        vm.repository.bump()
    }
    val photoPickerLauncher = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) scope.launch {
            runCatching { vm.repository.captureMoment(uri, "Foto de galería", selectedDate, true, appendToJournal = currentTab == LumenNavTab.Journal) }
                .onSuccess(vm::openObject).onFailure { android.widget.Toast.makeText(context, it.message ?: "No se pudo importar la imagen", android.widget.Toast.LENGTH_LONG).show() }
        }
    }
    val filePickerLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) scope.launch {
            runCatching { vm.repository.importFile(uri) }.onSuccess(vm::openObject)
                .onFailure { android.widget.Toast.makeText(context, it.message ?: "No se pudo importar el archivo", android.widget.Toast.LENGTH_LONG).show() }
        }
    }

    fun capture(date: String) {
        captureLauncher.launch(Intent(context, CaptureMomentActivity::class.java).putExtra("date", date))
    }
    fun quickNote(date: String) {
        quickNoteLauncher.launch(Intent(context, QuickNoteActivity::class.java).putExtra("date", date))
    }

    // Modal Bottom Sheet de Creación (+)
    if (showCreateSheet) {
        CreateModalBottomSheet(
            sheetState = sheetState,
            vm = vm,
            onDismiss = {
                scope.launch { sheetState.hide() }.invokeOnCompletion {
                    showCreateSheet = false
                }
            },
            onOpenToday = {
                selectedDate = todayIso()
                currentTab = LumenNavTab.Journal
            },
            onCaptureMoment = { capture(selectedDate) },
            onPickFile = { filePickerLauncher.launch(arrayOf("*/*")) },
            onPickImage = { photoPickerLauncher.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)) },
            onObjectCreated = { newObj ->
                vm.openObject(newObj)
            }
        )
    }

    val screenStates = rememberSaveableStateHolder()
    val retainedObjectStates = remember { mutableSetOf<String>() }
    val objectEntries = vm.objectHistory.entries
    LaunchedEffect(objectEntries) {
        val retained = objectEntries.map { "object-${it.key}" }.toSet()
        (retainedObjectStates - retained).forEach(screenStates::removeState)
        retainedObjectStates.clear()
        retainedObjectStates.addAll(retained)
    }
    val destinationKey = when {
        activeObj != null -> "object-${vm.objectHistory.current!!.key}"
        showCalendar -> "calendar"
        showSettings -> "settings"
        else -> "tab-${currentTab.name}-${vm.selectedTypeKey.orEmpty()}"
    }
    screenStates.SaveableStateProvider(destinationKey) {
        // Active Object Views (WebLink, Book, Image)
        if (activeObj != null) {
            when {
                activeObj.typeId == "query" -> {
                    val scopeId = activeObj.properties.optJSONObject("view")?.optString("type_id").orEmpty()
                    ObjectTypeCollectionScreen(vm, ObjectTypeItem(scopeId, activeObj.title, 0),
                        vm.objects().filter { scopeId.isBlank() || it.typeId == scopeId },
                        onBack = { vm.closeObject() }, onOpenObject = vm::openObject, initialView = activeObj)
                    return@SaveableStateProvider
                }
                activeObj.isWeb -> {
                    WebLinkDetailScreen(
                        obj = activeObj,
                        vm = vm,
                        onBack = { vm.closeObject() }
                    )
                    return@SaveableStateProvider
                }
                activeObj.isBook -> {
                    BookDetailScreen(
                        obj = activeObj,
                        vm = vm,
                        onBack = { vm.closeObject() }
                    )
                    return@SaveableStateProvider
                }
                activeObj.isImage -> {
                    ImageDetailScreen(
                        obj = activeObj,
                        vm = vm,
                        onBack = { vm.closeObject() }
                    )
                    return@SaveableStateProvider
                }
                else -> {
                    // Default fallback detail for Pages, Quotes, Notes, etc.
                    PageDetailScreen(
                        obj = activeObj,
                        vm = vm,
                        onBack = { vm.closeObject() }
                    )
                    return@SaveableStateProvider
                }
            }
        }

        // Calendar Screen
        if (showCalendar) {
            CalendarScreen(
                vm = vm,
                revision = revision,
                onBack = { showCalendar = false },
                onSelect = {
                    selectedDate = it
                    showCalendar = false
                    currentTab = LumenNavTab.Journal
                },
            )
            return@SaveableStateProvider
        }

        // Settings Screen
        if (showSettings) {
            SettingsScreen(
                vm = vm,
                revision = revision,
                onLock = onLock,
                onBack = { showSettings = false }
            )
            return@SaveableStateProvider
        }

        // Main App Shell with Floating Dock
        Scaffold { padding ->
            Box(
                modifier = Modifier
                    .fillMaxSize()
            ) {
                // Main Content Area
                when (currentTab) {
                    LumenNavTab.Home -> HomeScreen(
                        vm = vm,
                        revision = revision,
                        onOpenObject = { obj -> vm.openObject(obj) },
                        onOpenSettings = { showSettings = true },
                        onOpenCreate = { showCreateSheet = true }
                    )
                    LumenNavTab.Journal -> JournalScreen(
                        vm = vm,
                        revision = revision,
                        date = selectedDate,
                        onDateChange = { selectedDate = it },
                        onCapture = { capture(selectedDate) },
                        onQuickNote = { quickNote(selectedDate) },
                        onOpenCalendar = { showCalendar = true },
                        onOpenObject = { obj -> vm.openObject(obj) }
                    )
                    LumenNavTab.Tasks -> TasksScreen(
                        vm = vm,
                        revision = revision,
                        onOpenTask = { obj -> vm.openObject(obj) }
                    )
                    LumenNavTab.Search -> SearchScreen(
                        vm = vm,
                        revision = revision,
                        onOpenObject = { obj -> vm.openObject(obj) }
                    )
                }

                if (WindowInsets.ime.getBottom(LocalDensity.current) == 0) LumenDock(
                    currentTab = currentTab,
                    onSelectTab = { currentTab = it },
                    onOpenCreate = { showCreateSheet = true },
                    onOpenSettings = { showSettings = true },
                    modifier = Modifier.align(Alignment.BottomCenter)
                )
            }
        }
    }
}
