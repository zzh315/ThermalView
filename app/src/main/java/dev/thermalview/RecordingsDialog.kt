package dev.thermalview

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** A recording in the dumps folder, as the list shows it. */
class RecordingEntry(val base: String, val title: String, val note: String)

/**
 * The recordings, newest first: each by its date and time (from its name, dump_YYYYMMDD_HHMMSS),
 * with its length and source from the sidecar JSON.
 */
fun listRecordings(dir: String): List<RecordingEntry> {
    val stamp = java.text.SimpleDateFormat("yyyyMMdd_HHmmss", java.util.Locale.US)
    val shown = java.text.SimpleDateFormat("EEE d MMM, HH:mm:ss", java.util.Locale.getDefault())
    return File(dir).listFiles { f -> f.name.endsWith(".raw") }.orEmpty()
        .sortedByDescending { it.name }
        .map { raw ->
            val base = raw.path.removeSuffix(".raw")
            val name = raw.name.removeSuffix(".raw")
            val date = runCatching { stamp.parse(name.removePrefix("dump_")) }.getOrNull()
            val meta = runCatching { org.json.JSONObject(File("$base.json").readText()) }.getOrNull()
            val frames = meta?.optInt("frame_count", 0)?.takeIf { it > 0 }
            val note = listOfNotNull(
                frames?.let { "$it frames (%.0f s)".format(it / 25.0) },
                meta?.optString("source")?.takeIf { it.isNotEmpty() && it != "camera" },
                "%.0f MB".format(raw.length() / 1e6),
            ).joinToString(" · ")
            RecordingEntry(base, date?.let { shown.format(it) } ?: name, note)
        }
}

/** A small picture of a recording: its first frame, stretched between its 1st and 99th percentiles. */
private fun thumbnail(base: String): ImageBitmap? = runCatching {
    val w = 256
    val h = 192  // (the image rows; the frame's last 4 rows are metadata)
    val bytes = ByteArray(w * h * 2)
    RandomAccessFile("$base.raw", "r").use { it.readFully(bytes) }
    val px = ShortArray(w * h)
    ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(px)
    val v = IntArray(px.size) { px[it].toInt() and 0xFFFF }
    val sorted = v.copyOf().also { it.sort() }
    val lo = sorted[sorted.size / 100]
    val hi = maxOf(sorted[sorted.size * 99 / 100], lo + 1)
    val argb = IntArray(v.size) {
        val g = ((v[it] - lo) * 255 / (hi - lo)).coerceIn(0, 255)
        (0xFF shl 24) or (g shl 16) or (g shl 8) or g
    }
    Bitmap.createBitmap(argb, w, h, Bitmap.Config.ARGB_8888).asImageBitmap()
}.getOrNull()

/**
 * The recordings (owner, 2026-09-26: "a list of recordings, and in that list there are action buttons
 * to replay or delete recordings, there should also be ways to do bulk selection and deletion"): each
 * with a picture of its first frame, Replay and Delete; Select for several at once. Deleting asks first.
 */
@Composable
fun RecordingsDialog(dir: String, onDismiss: () -> Unit, onReplay: (RecordingEntry) -> Unit, say: (String) -> Unit) {
    var entries by remember { mutableStateOf<List<RecordingEntry>?>(null) }
    var reload by remember { mutableStateOf(0) }
    LaunchedEffect(dir, reload) { entries = withContext(Dispatchers.IO) { listRecordings(dir) } }
    val thumbs = remember { mutableStateMapOf<String, ImageBitmap?>() }
    var selecting by remember { mutableStateOf(false) }
    val selected = remember { mutableStateListOf<String>() }
    var confirm by remember { mutableStateOf<List<RecordingEntry>?>(null) }

    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(color = Ui.Panel, shape = RoundedCornerShape(20.dp), modifier = Modifier.width(760.dp).heightIn(max = 720.dp)) {
            Column(Modifier.padding(horizontal = 20.dp, vertical = 16.dp)) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    val list = entries.orEmpty()
                    Column(Modifier.weight(1f)) {
                        Text("Recordings", color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.SemiBold)
                        Text(
                            if (selecting) "${selected.size} selected"
                            else "${list.size} on the tablet · Capture records one; a replay pauses the camera until Exit replay",
                            color = Ui.Subtle, fontSize = 13.sp,
                        )
                    }
                    if (selecting) {
                        TextButton(onClick = {
                            if (selected.size == list.size) selected.clear()
                            else {
                                selected.clear()
                                selected.addAll(list.map { it.base })
                            }
                        }) { Text(if (selected.size == list.size && list.isNotEmpty()) "Select none" else "Select all") }
                        TextButton(
                            onClick = { confirm = list.filter { it.base in selected } },
                            enabled = selected.isNotEmpty(),
                        ) { Text("Delete (${selected.size})", color = if (selected.isNotEmpty()) Color(0xFFFF8A80) else Ui.Subtle) }
                        TextButton(onClick = {
                            selecting = false
                            selected.clear()
                        }) { Text("Done") }
                    } else {
                        if (list.isNotEmpty()) TextButton(onClick = { selecting = true }) { Text("Select") }
                        TextButton(onClick = onDismiss) { Text("Close") }
                    }
                }
                Spacer(Modifier.size(8.dp))
                val list = entries
                when {
                    list == null -> Text("Reading the recordings…", color = Ui.Subtle, modifier = Modifier.padding(16.dp))
                    list.isEmpty() -> Text("No recordings yet: Capture on the right bar records one.", color = Ui.Subtle,
                        modifier = Modifier.padding(16.dp))
                    else -> LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        items(list, key = { it.base }) { e ->
                            LaunchedEffect(e.base) {
                                if (e.base !in thumbs) thumbs[e.base] = withContext(Dispatchers.IO) { thumbnail(e.base) }
                            }
                            val isSelected = e.base in selected
                            Row(
                                Modifier.fillMaxWidth()
                                    .background(if (isSelected) Ui.TileActive else Ui.Tile, RoundedCornerShape(14.dp))
                                    .clickable {
                                        if (selecting) {
                                            if (isSelected) selected.remove(e.base) else selected.add(e.base)
                                        } else {
                                            onReplay(e)
                                        }
                                    }
                                    .padding(10.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                if (selecting) {
                                    Checkbox(checked = isSelected, onCheckedChange = {
                                        if (it) selected.add(e.base) else selected.remove(e.base)
                                    })
                                    Spacer(Modifier.size(4.dp))
                                }
                                Box(
                                    Modifier.size(width = 96.dp, height = 72.dp).background(Color.Black, RoundedCornerShape(8.dp))
                                        .border(1.dp, Color(0x33FFFFFF), RoundedCornerShape(8.dp)),
                                    contentAlignment = Alignment.Center,
                                ) {
                                    thumbs[e.base]?.let {
                                        Image(it, contentDescription = null, modifier = Modifier.size(width = 94.dp, height = 70.dp))
                                    }
                                }
                                Spacer(Modifier.size(14.dp))
                                Column(Modifier.weight(1f)) {
                                    Text(e.title, color = Color.White, fontSize = 16.sp)
                                    Text(e.note, color = Ui.Subtle, fontSize = 12.sp)
                                }
                                if (!selecting) {
                                    FilledTonalButton(onClick = { onReplay(e) }) { Text("Replay") }
                                    Spacer(Modifier.size(6.dp))
                                    TextButton(onClick = { confirm = listOf(e) }) { Text("Delete", color = Color(0xFFFF8A80)) }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    confirm?.let { doomed ->
        AlertDialog(
            onDismissRequest = { confirm = null },
            title = { Text(if (doomed.size == 1) "Delete this recording?" else "Delete ${doomed.size} recordings?") },
            text = { Text(if (doomed.size == 1) doomed[0].title + " goes for good." else "They go for good.") },
            confirmButton = {
                TextButton(onClick = {
                    confirm = null
                    var gone = 0
                    for (e in doomed) {
                        if (File(e.base + ".raw").delete()) gone++
                        File(e.base + ".json").delete()
                        thumbs.remove(e.base)
                        selected.remove(e.base)
                    }
                    say(if (gone == 1) "Deleted a recording" else "Deleted $gone recordings")
                    if (selected.isEmpty()) selecting = false
                    reload++
                }) { Text("Delete", color = Color(0xFFFF8A80)) }
            },
            dismissButton = { TextButton(onClick = { confirm = null }) { Text("Cancel") } },
        )
    }
}
