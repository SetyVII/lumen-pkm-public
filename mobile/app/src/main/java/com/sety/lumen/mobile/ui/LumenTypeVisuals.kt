package com.sety.lumen.mobile.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AlternateEmail
import androidx.compose.material.icons.filled.Audiotrack
import androidx.compose.material.icons.filled.Book
import androidx.compose.material.icons.filled.Bookmark
import androidx.compose.material.icons.filled.CalendarMonth
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.FormatQuote
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Language
import androidx.compose.material.icons.filled.Lightbulb
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material.icons.filled.Notes
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.PictureAsPdf
import androidx.compose.material.icons.filled.SyncAlt
import androidx.compose.material.icons.filled.TableChart
import androidx.compose.material.icons.filled.Tag
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.clickable


data class LumenTypeVisual(
    val key: String,
    val label: String,
    val icon: ImageVector,
    val darkContainer: Color,
    val darkContent: Color,
    val lightContainer: Color,
    val lightContent: Color,
)

object LumenTypeVisuals {
    val Quote = LumenTypeVisual(
        "quote", "Cita", Icons.Default.FormatQuote,
        Color(0xFF5A2C3A), Color(0xFFFECDD3), Color(0xFFFCE7F3), Color(0xFF9D174D),
    )
    val Definition = LumenTypeVisual(
        "definition", "Definición", Icons.Default.Bookmark,
        Color(0xFF453361), Color(0xFFDDD6FE), Color(0xFFEDE9FE), Color(0xFF5B21B6),
    )
    val Image = LumenTypeVisual(
        "image", "Imagen", Icons.Default.Image,
        Color(0xFF573030), Color(0xFFFECACA), Color(0xFFFEE2E2), Color(0xFF991B1B),
    )
    val Web = LumenTypeVisual(
        "web", "Enlace web", Icons.Default.Language,
        Color(0xFF2F3957), Color(0xFFBFDBFE), Color(0xFFDBEAFE), Color(0xFF1E40AF),
    )
    val Book = LumenTypeVisual(
        "book", "Libro", Icons.Default.Book,
        Color(0xFF543429), Color(0xFFFED7AA), Color(0xFFFFEDD5), Color(0xFF9A3412),
    )
    val Page = LumenTypeVisual(
        "page", "Página", Icons.Default.Description,
        Color(0xFF2F3957), Color(0xFFBFDBFE), Color(0xFFDBEAFE), Color(0xFF1E40AF),
    )
    val Note = LumenTypeVisual(
        "note", "Nota", Icons.Default.Notes,
        Color(0xFF493625), Color(0xFFFEF08A), Color(0xFFFEF9C3), Color(0xFF854D0E),
    )
    val Task = LumenTypeVisual(
        "task", "Tarea", Icons.Default.CheckCircle,
        Color(0xFF35467D), Color(0xFFEFF6FF), Color(0xFFE0E7FF), Color(0xFF3730A3),
    )
    val Journal = LumenTypeVisual(
        "journal", "Nota diaria", Icons.Default.CalendarMonth,
        Color(0xFF493625), Color(0xFFFEF08A), Color(0xFFFEF9C3), Color(0xFF854D0E),
    )
    val Code = LumenTypeVisual(
        "snippet", "Fragmento de código", Icons.Default.Code,
        Color(0xFF452F57), Color(0xFFE9D5FF), Color(0xFFF3E8FF), Color(0xFF6B21A8),
    )
    val Media = LumenTypeVisual(
        "media", "Medio", Icons.Default.Movie,
        Color(0xFF452F57), Color(0xFFEEEEEE), Color(0xFFF3E8FF), Color(0xFF6B21A8),
    )
    val Person = LumenTypeVisual(
        "person", "Persona", Icons.Default.Person,
        Color(0xFF5A2C3A), Color(0xFFFECDD3), Color(0xFFFCE7F3), Color(0xFF9D174D),
    )
    val Idea = LumenTypeVisual(
        "idea", "Idea", Icons.Default.Lightbulb,
        Color(0xFF493625), Color(0xFFFEF08A), Color(0xFFFEF9C3), Color(0xFF854D0E),
    )
    val Tag = LumenTypeVisual(
        "tag", "Etiqueta", Icons.Default.Tag,
        Color(0xFF452F57), Color(0xFFE9D5FF), Color(0xFFF3E8FF), Color(0xFF6B21A8),
    )
    val Table = LumenTypeVisual(
        "table", "Tabla", Icons.Default.TableChart,
        Color(0xFF543429), Color(0xFFFED7AA), Color(0xFFFFEDD5), Color(0xFF9A3412),
    )
    val Pdf = LumenTypeVisual(
        "pdf", "PDF", Icons.Default.PictureAsPdf,
        Color(0xFF573030), Color(0xFFFECACA), Color(0xFFFEE2E2), Color(0xFF991B1B),
    )
    val Audio = LumenTypeVisual(
        "audio", "Audio", Icons.Default.Audiotrack,
        Color(0xFF573030), Color(0xFFFECACA), Color(0xFFFEE2E2), Color(0xFF991B1B),
    )
    val Tweet = LumenTypeVisual(
        "tweet", "Tweet", Icons.Default.AlternateEmail,
        Color(0xFF2F3957), Color(0xFFBFDBFE), Color(0xFFDBEAFE), Color(0xFF1E40AF),
    )
    val Resource = LumenTypeVisual(
        "resource", "Recurso", Icons.Default.Folder,
        Color(0xFF563929), Color(0xFFF5F5F5), Color(0xFFFEF3C7), Color(0xFF92400E),
    )
    val Default = LumenTypeVisual(
        "default", "Objeto", Icons.Default.Notes,
        Color(0xFF2D2D31), Color(0xFFE4E3E2), Color(0xFFE5E7EB), Color(0xFF374151),
    )

    fun forType(typeId: String): LumenTypeVisual = when (typeId.lowercase()) {
        "quote", "cita" -> Quote
        "definition", "definicion", "definición" -> Definition
        "image", "imagen", "photo", "foto" -> Image
        "web", "link", "enlace", "enlace web" -> Web
        "book", "libro" -> Book
        "page", "pagina", "página" -> Page
        "note", "nota", "atomic" -> Note
        "task", "tarea" -> Task
        "journal", "diario", "nota diaria" -> Journal
        "snippet", "code", "codigo", "código", "fragmento de código" -> Code
        "media", "medio", "video", "vídeo", "podcast" -> Media
        "person", "persona" -> Person
        "idea" -> Idea
        "tag", "etiqueta" -> Tag
        "table", "tabla" -> Table
        "pdf" -> Pdf
        "audio" -> Audio
        "tweet" -> Tweet
        "resource", "recurso", "project", "collection", "file" -> Resource
        else -> Default
    }
}

@Composable
fun LumenTypeBadge(
    typeId: String,
    size: Dp = 30.dp,
    iconSize: Dp = size * 0.55f,
) {
    val visual = LumenTypeVisuals.forType(typeId)
    val dark = isSystemInDarkTheme()
    Surface(
        modifier = Modifier.size(size),
        shape = RoundedCornerShape(size * 0.3f),
        color = if (dark) visual.darkContainer else visual.lightContainer,
    ) {
        androidx.compose.foundation.layout.Box(
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                visual.icon,
                contentDescription = null,
                tint = if (dark) visual.darkContent else visual.lightContent,
                modifier = Modifier.size(iconSize),
            )
        }
    }
}

@Composable
fun LumenTypeChip(
    typeId: String,
    label: String? = null,
    showSwap: Boolean = false,
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
) {
    val visual = LumenTypeVisuals.forType(typeId)
    val dark = isSystemInDarkTheme()
    val container = if (dark) visual.darkContainer else visual.lightContainer
    val content = if (dark) visual.darkContent else visual.lightContent
    Surface(
        modifier = modifier.then(
            if (onClick != null) Modifier.clip(RoundedCornerShape(8.dp)).clickable(onClick = onClick) else Modifier
        ),
        shape = RoundedCornerShape(8.dp),
        color = container,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(visual.icon, contentDescription = null, tint = content, modifier = Modifier.size(14.dp))
            Spacer(Modifier.width(4.dp))
            Text(
                label ?: visual.label,
                style = MaterialTheme.typography.labelSmall,
                color = content,
                fontWeight = FontWeight.SemiBold,
            )
            if (showSwap) {
                Spacer(Modifier.width(4.dp))
                Icon(Icons.Default.SyncAlt, contentDescription = null, tint = content, modifier = Modifier.size(12.dp))
            }
        }
    }
}
