package dev.thermalview

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupPositionProvider
import androidx.compose.ui.window.PopupProperties
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

// The bars' menus (owner, 2026-09-26: "should open to the right, also it should look better and modern
// and smooth"): a card beside the tile that opened it, on the image's side (right of the left bar, left
// of the right bar; below the top row and above the bottom one in portrait). It grows out of that side
// and fades in; choices are cards whose highlight glides to the one picked.

/** Where a menu opens from its tile: toward the image. */
enum class PopupSide { Right, Left, Below, Above }

private class SidePosition(val side: PopupSide, val gap: Int, val margin: Int) : PopupPositionProvider {
    override fun calculatePosition(anchorBounds: IntRect, windowSize: IntSize, layoutDirection: LayoutDirection, popupContentSize: IntSize): IntOffset {
        val w = popupContentSize.width
        val h = popupContentSize.height
        val (x, y) = when (side) {
            PopupSide.Right -> anchorBounds.right + gap to anchorBounds.top
            PopupSide.Left -> anchorBounds.left - gap - w to anchorBounds.top
            PopupSide.Below -> anchorBounds.left to anchorBounds.bottom + gap
            PopupSide.Above -> anchorBounds.right - w to anchorBounds.top - gap - h
        }
        // (kept on screen: a tall menu beside a low tile moves up)
        return IntOffset(
            x.coerceIn(margin, maxOf(margin, windowSize.width - w - margin)),
            y.coerceIn(margin, maxOf(margin, windowSize.height - h - margin)),
        )
    }
}

/**
 * A menu beside its tile (put it in the same Box as the tile): open while [expanded], closed by a tap
 * outside or Back through [onDismiss].
 */
@Composable
fun SidePopup(expanded: Boolean, onDismiss: () -> Unit, side: PopupSide, width: Dp, content: @Composable ColumnScope.() -> Unit) {
    val visible = remember { MutableTransitionState(false) }
    visible.targetState = expanded
    if (!visible.currentState && !visible.targetState) return
    val density = LocalDensity.current
    val position = remember(side, density) {
        SidePosition(side, with(density) { 10.dp.roundToPx() }, with(density) { 8.dp.roundToPx() })
    }
    // It grows from the side facing its tile.
    val origin = when (side) {
        PopupSide.Right -> TransformOrigin(0f, 0.15f)
        PopupSide.Left -> TransformOrigin(1f, 0.15f)
        PopupSide.Below -> TransformOrigin(0.15f, 0f)
        PopupSide.Above -> TransformOrigin(0.85f, 1f)
    }
    Popup(popupPositionProvider = position, onDismissRequest = onDismiss, properties = PopupProperties(focusable = true)) {
        AnimatedVisibility(
            visibleState = visible,
            enter = fadeIn(tween(150)) + scaleIn(tween(200, easing = FastOutSlowInEasing), initialScale = 0.9f, transformOrigin = origin),
            exit = fadeOut(tween(110)) + scaleOut(tween(110), targetScale = 0.96f, transformOrigin = origin),
        ) {
            Surface(
                color = Ui.Menu,
                shape = RoundedCornerShape(20.dp),
                border = BorderStroke(1.dp, Color(0x1FFFFFFF)),
                shadowElevation = 12.dp,
                modifier = Modifier.width(width),
            ) {
                Column(Modifier.padding(horizontal = 14.dp, vertical = 14.dp), content = content)
            }
        }
    }
}

/** A section's title in a menu. */
@Composable
fun MenuHeading(text: String, modifier: Modifier = Modifier) {
    Text(
        text.uppercase(), color = Ui.Subtle, fontSize = 11.sp, fontWeight = FontWeight.SemiBold, letterSpacing = 0.9.sp,
        modifier = modifier.padding(start = 2.dp, bottom = 8.dp),
    )
}

/**
 * A choice in a menu: a card, highlighted (tinted, with an accent edge and a check) when [selected];
 * the highlight fades between cards as the choice changes.
 */
@Composable
fun ChoiceCard(selected: Boolean, onClick: () -> Unit, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    val back by animateColorAsState(if (selected) Ui.TileActive else Ui.MenuCard, tween(180), label = "card")
    val edge by animateColorAsState(if (selected) Ui.Accent else Color.Transparent, tween(180), label = "edge")
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val shape = RoundedCornerShape(14.dp)
    Column(
        modifier.background(if (pressed) lerp(back, Color.White, 0.08f) else back, shape).border(1.5.dp, edge, shape)
            .clickable(interactionSource = interaction, indication = null, onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 9.dp),
        content = content,
    )
}

/** A choice card's name (and a detail under it), with a check when it's the one chosen and [check]. */
@Composable
fun ChoiceName(name: String, selected: Boolean, detail: String? = null, check: Boolean = false) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(name, color = Ui.Text, fontSize = if (check) 15.sp else 14.sp, fontWeight = FontWeight.Medium, maxLines = 1)
            if (detail != null) Text(detail, color = Ui.Subtle, fontSize = 11.sp, maxLines = 1)
        }
        if (check && selected) CheckGlyph()
    }
}

@Composable
private fun CheckGlyph() {
    Canvas(Modifier.size(14.dp)) {
        val w = 2.dp.toPx()
        drawLine(Ui.Accent, Offset(size.width * 0.12f, size.height * 0.55f), Offset(size.width * 0.4f, size.height * 0.82f), w, StrokeCap.Round)
        drawLine(Ui.Accent, Offset(size.width * 0.4f, size.height * 0.82f), Offset(size.width * 0.9f, size.height * 0.2f), w, StrokeCap.Round)
    }
}

/** A palette's colors, cold to hot, as a strip. */
@Composable
fun Swatch(colors: List<Color>, modifier: Modifier, height: Dp = 6.dp) {
    Box(
        modifier.height(height)
            .background(Brush.horizontalGradient(colors.ifEmpty { listOf(Color.Black, Color.White) }), RoundedCornerShape(height / 2)),
    )
}

/**
 * The palette picker: White hot or Rainbow, each shown by its colors; with Rainbow, its looks too (Deep
 * or Soft), which slide open under it. Picking closes it, except Rainbow, which stays open for its looks.
 */
@Composable
fun PalettePicker(options: DebugOptions, onOptions: (DebugOptions) -> Unit, colors: (Int, Int) -> List<Color>, done: () -> Unit) {
    MenuHeading("Palette")
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        PaletteCard("White hot", colors(1, 0), options.palette == 1) {
            onOptions(options.copy(palette = 1))
            done()
        }
        PaletteCard("Rainbow", colors(2, options.rainbowPreset), options.palette == 2) { onOptions(options.copy(palette = 2)) }
    }
    val rainbow = remember { MutableTransitionState(options.palette == 2) }
    rainbow.targetState = options.palette == 2
    AnimatedVisibility(
        visibleState = rainbow,
        enter = expandVertically(tween(220, easing = FastOutSlowInEasing)) + fadeIn(tween(220)),
        exit = shrinkVertically(tween(160)) + fadeOut(tween(120)),
    ) {
        Column {
            Spacer(Modifier.height(14.dp))
            MenuHeading("Rainbow colors")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                MainActivity.RAINBOW_PRESETS.forEachIndexed { i, preset ->
                    ChoiceCard(options.rainbowPreset == i, {
                        onOptions(options.copy(rainbowPreset = i))
                        done()
                    }, Modifier.weight(1f)) {
                        Swatch(colors(2, i), Modifier.fillMaxWidth(), height = 14.dp)
                        Spacer(Modifier.height(7.dp))
                        ChoiceName(preset.name, options.rainbowPreset == i)
                    }
                }
            }
        }
    }
}

@Composable
private fun PaletteCard(name: String, swatch: List<Color>, selected: Boolean, onPick: () -> Unit) {
    ChoiceCard(selected, onPick, Modifier.fillMaxWidth()) {
        Swatch(swatch, Modifier.fillMaxWidth(), height = 20.dp)
        Spacer(Modifier.height(8.dp))
        ChoiceName(name, selected, check = true)
    }
}

/**
 * The view menu (owner, 2026-09-26: "a menu for selecting view size as well as orientation (there
 * should be auto orientation that changes with device orientation as well)"): the view sizes, and how
 * the screen turns: with the tablet (Auto), or held in landscape or portrait (either way up). A pick
 * applies at once and closes the menu.
 */
@Composable
fun ViewMenu(options: DebugOptions, onOptions: (DebugOptions) -> Unit, done: () -> Unit) {
    MenuHeading("View size")
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        MainActivity.VIEW_NAMES.forEachIndexed { i, name ->
            ChoiceCard(options.viewSize == i, {
                onOptions(options.copy(viewSize = i))
                done()
            }, Modifier.weight(1f)) {
                SizeGlyph(i, options.viewSize == i)
                Spacer(Modifier.height(8.dp))
                ChoiceName(name, options.viewSize == i, MainActivity.VIEW_DIAGONALS.getOrElse(i) { "" })
            }
        }
    }
    Spacer(Modifier.height(14.dp))
    MenuHeading("Orientation")
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        MainActivity.ORIENTATION_NAMES.forEachIndexed { i, name ->
            ChoiceCard(options.orientation == i, {
                onOptions(options.copy(orientation = i))
                done()
            }, Modifier.weight(1f)) {
                OrientationGlyph(i, options.orientation == i)
                Spacer(Modifier.height(8.dp))
                ChoiceName(name, options.orientation == i)
            }
        }
    }
    Spacer(Modifier.height(10.dp))
    Text(
        MainActivity.ORIENTATION_NOTES.getOrElse(options.orientation) { "" }, color = Ui.Subtle, fontSize = 12.sp,
        lineHeight = 15.sp, modifier = Modifier.padding(horizontal = 2.dp),
    )
}

/** A view size: its frame, drawn to scale with the others inside the largest's outline. */
@Composable
private fun SizeGlyph(size: Int, selected: Boolean) {
    Canvas(Modifier.size(width = 36.dp, height = 27.dp)) {
        val c = if (selected) Ui.Accent else Ui.Subtle
        val f = listOf(0.41f, 0.69f, 1f).getOrElse(size) { 1f }  // (880, 1467 and 2133 px wide)
        val w = 1.5.dp.toPx()
        drawRoundRect(c.copy(alpha = 0.3f), Offset(w / 2, w / 2), Size(this.size.width - w, this.size.height - w), CornerRadius(3.dp.toPx()), style = Stroke(w))
        val iw = (this.size.width - w) * f
        val ih = (this.size.height - w) * f
        drawRoundRect(c, Offset((this.size.width - iw) / 2, (this.size.height - ih) / 2), Size(iw, ih), CornerRadius(2.dp.toPx()), style = Stroke(w))
    }
}

/** An orientation: the tablet in landscape or in portrait, or turning (Auto). */
@Composable
private fun OrientationGlyph(orientation: Int, selected: Boolean) {
    Canvas(Modifier.size(width = 36.dp, height = 27.dp)) {
        val c = if (selected) Ui.Accent else Ui.Subtle
        val w = 1.5.dp.toPx()
        when (orientation) {
            1 -> tablet(c, Size(30.dp.toPx(), 21.dp.toPx()), w)
            2 -> tablet(c, Size(17.dp.toPx(), 25.dp.toPx()), w)
            else -> {
                tablet(c, Size(12.dp.toPx(), 17.dp.toPx()), w)
                // An arc around it with an arrowhead: it turns.
                val r = 12.5.dp.toPx()
                val center = Offset(this.size.width / 2, this.size.height / 2)
                drawArc(c, -30f, 120f, false, center - Offset(r, r), Size(2 * r, 2 * r), style = Stroke(w, cap = StrokeCap.Round))
                drawArc(c, 150f, 120f, false, center - Offset(r, r), Size(2 * r, 2 * r), style = Stroke(w, cap = StrokeCap.Round))
                for (end in listOf(90f, 270f)) {
                    val a = end * PI.toFloat() / 180f
                    val tip = center + Offset(cos(a) * r, sin(a) * r)
                    val back = Offset(-sin(a), cos(a)) * -1f  // (back along the arc, against the turn)
                    val out = Offset(cos(a), sin(a))
                    val l = 3.5.dp.toPx()
                    drawLine(c, tip, tip + (back + out) * (l * 0.7f), w, StrokeCap.Round)
                    drawLine(c, tip, tip + (back - out) * (l * 0.7f), w, StrokeCap.Round)
                }
            }
        }
    }
}

private fun DrawScope.tablet(c: Color, s: Size, w: Float) {
    val tl = Offset((size.width - s.width) / 2, (size.height - s.height) / 2)
    drawRoundRect(c, tl, s, CornerRadius(3.dp.toPx()), style = Stroke(w))
    drawRoundRect(c.copy(alpha = 0.25f), tl + Offset(3.dp.toPx(), 3.dp.toPx()), Size(s.width - 6.dp.toPx(), s.height - 6.dp.toPx()), CornerRadius(1.5.dp.toPx()))
}
