package dev.thermalview

import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color

/** The app's colors: dark and quiet, so nothing competes with the image. */
internal object Ui {
    val Tile = Color(0xFF1A1F27)
    val TileActive = Color(0xFF24354D)
    val TileBusy = Color(0xFF4A2A12)       // a capture in progress
    val TileAlert = Color(0xFF55161F)      // the replay badge
    val Panel = Color(0xF5141920)
    val Text = Color(0xFFF1F3F6)
    val Subtle = Color(0xFF9AA4B2)
    val Faint = Color(0x33FFFFFF)
    val Accent = Color(0xFF7FB2FF)
    val Warning = Color(0xFFFFB74D)
    val LabelBack = Color(0xC0101418)      // behind the readouts and banners over the image

    // The markers: hottest point, coldest point, center (and their text, lighter to read on dark).
    val Hot = Color(0xFFFF453A)
    val HotText = Color(0xFFFF9C94)
    val Cold = Color(0xFF3D9BFF)
    val ColdText = Color(0xFF9CCFFF)
    val Center = Color.White

    val Scheme = darkColorScheme(
        primary = Accent,
        onPrimary = Color(0xFF0D2240),  // (a switch's thumb when on)
        secondaryContainer = Color(0xFF2C3E57),
        onSecondaryContainer = Color.White,
        surface = Color(0xFF1A1F27),
        surfaceContainer = Color(0xFF1E242D),
    )
}
