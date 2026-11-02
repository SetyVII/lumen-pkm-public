package com.sety.lumen.mobile.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp


object LumenColors {
    val Background = Color(0xFF151518)
    val BackgroundDeep = Color(0xFF0D0D10)
    val SurfaceTop = Color(0xFF19191C)
    val Surface = Color(0xFF1A1A1D)
    val SurfaceHigh = Color(0xFF232326)
    val SurfaceElevated = Color(0xFF2D2D31)
    val Selected = Color(0xFF3F3F41)
    val Border = Color(0xFF2A2A2E)
    val BorderStrong = Color(0xFF3A3A3E)
    val Text = Color(0xFFE4E3E2)
    val TextStrong = Color(0xFFFFFFFF)
    val TextMuted = Color(0xFFA7A2A1)
    val TextFaint = Color(0xFF767576)
    val Accent = Color(0xFFFB2C36)
    val AccentSoft = Color(0xFF3A1D20)
    val Link = Color(0xFF50A2FF)
    val WorkspacePill = Color(0xFF4D4746)
    val EditorBar = Color(0xFF1D2024)
    val EditorBarBorder = Color(0xFF2E3238)
}

private val Light = lightColorScheme(
    primary = Color(0xFFC92A34),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFFFE1E2),
    onPrimaryContainer = Color(0xFF410006),
    secondary = Color(0xFF6B6869),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFEFEDE8),
    onSecondaryContainer = Color(0xFF1E1E20),
    tertiary = Color(0xFF2563EB),
    onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFFDBEAFE),
    onTertiaryContainer = Color(0xFF1E3A8A),
    background = Color(0xFFF7F6F3),
    onBackground = Color(0xFF1E1E20),
    surface = Color(0xFFFFFFFF),
    onSurface = Color(0xFF1E1E20),
    surfaceVariant = Color(0xFFEFEDE8),
    onSurfaceVariant = Color(0xFF6B6869),
    outline = Color(0xFFC9C5BE),
    outlineVariant = Color(0xFFDEDBD4),
    error = Color(0xFFB3261E),
    onError = Color(0xFFFFFFFF),
    errorContainer = Color(0xFFFFDAD6),
    onErrorContainer = Color(0xFF410002),
)

private val Dark = darkColorScheme(
    primary = LumenColors.Accent,
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = LumenColors.AccentSoft,
    onPrimaryContainer = Color(0xFFFFD9DB),
    secondary = LumenColors.TextMuted,
    onSecondary = LumenColors.Background,
    secondaryContainer = LumenColors.Selected,
    onSecondaryContainer = Color(0xFFFFFFFF),
    tertiary = LumenColors.Link,
    onTertiary = Color(0xFF0B1B2E),
    tertiaryContainer = Color(0xFF1B2C42),
    onTertiaryContainer = Color(0xFFBFDBFE),
    background = LumenColors.Background,
    onBackground = LumenColors.Text,
    surface = LumenColors.Surface,
    onSurface = Color(0xFFFFFFFF),
    surfaceVariant = LumenColors.SurfaceHigh,
    onSurfaceVariant = LumenColors.TextMuted,
    outline = LumenColors.BorderStrong,
    outlineVariant = LumenColors.Border,
    error = Color(0xFFFF6B6B),
    onError = Color(0xFF2A0A0A),
    errorContainer = LumenColors.AccentSoft,
    onErrorContainer = Color(0xFFFFD9DB),
)

private val LumenShapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp),
    small = RoundedCornerShape(10.dp),
    medium = RoundedCornerShape(12.dp),
    large = RoundedCornerShape(16.dp),
    extraLarge = RoundedCornerShape(24.dp),
)

private val LumenTypography = Typography(
    displaySmall = TextStyle(
        fontSize = 34.sp,
        lineHeight = 40.sp,
        fontWeight = FontWeight.Bold,
        letterSpacing = (-0.5).sp,
    ),
    headlineMedium = TextStyle(
        fontSize = 28.sp,
        lineHeight = 34.sp,
        fontWeight = FontWeight.Bold,
        letterSpacing = (-0.3).sp,
    ),
    headlineSmall = TextStyle(
        fontSize = 24.sp,
        lineHeight = 30.sp,
        fontWeight = FontWeight.Bold,
        letterSpacing = (-0.2).sp,
    ),
    titleLarge = TextStyle(
        fontSize = 20.sp,
        lineHeight = 26.sp,
        fontWeight = FontWeight.SemiBold,
    ),
    titleMedium = TextStyle(
        fontSize = 16.sp,
        lineHeight = 22.sp,
        fontWeight = FontWeight.SemiBold,
    ),
    titleSmall = TextStyle(
        fontSize = 14.sp,
        lineHeight = 20.sp,
        fontWeight = FontWeight.SemiBold,
    ),
    bodyLarge = TextStyle(
        fontSize = 16.sp,
        lineHeight = 24.sp,
    ),
    bodyMedium = TextStyle(
        fontSize = 14.sp,
        lineHeight = 20.sp,
    ),
    bodySmall = TextStyle(
        fontSize = 12.sp,
        lineHeight = 16.sp,
    ),
    labelLarge = TextStyle(
        fontSize = 14.sp,
        lineHeight = 18.sp,
        fontWeight = FontWeight.Medium,
    ),
    labelMedium = TextStyle(
        fontSize = 12.sp,
        lineHeight = 16.sp,
        fontWeight = FontWeight.Medium,
    ),
    labelSmall = TextStyle(
        fontSize = 11.sp,
        lineHeight = 14.sp,
    ),
)

@Composable
fun LumenTheme(darkTheme: Boolean = isSystemInDarkTheme(), content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = if (darkTheme) Dark else Light,
        typography = LumenTypography,
        shapes = LumenShapes,
        content = content,
    )
}
