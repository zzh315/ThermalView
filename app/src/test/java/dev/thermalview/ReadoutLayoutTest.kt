package dev.thermalview

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** The labels' placement (Readouts.kt): inside the view, clear of each other and of what covers the image. */
class ReadoutLayoutTest {
    private val inner = Rect(12f, 12f, 988f, 738f)  // (the overlay's 6 dp inset at density 2)
    private val size = Size(120f, 40f)
    private val arm = 26f

    private fun place(at: Offset, obstacles: List<Rect> = emptyList(), remembered: Int = -1) =
        placeLabel(labelPlaces(at, arm, 8f, size), size, inner, obstacles, remembered, stickiness = 4f)

    private fun inside(r: Rect) = r.left >= inner.left && r.top >= inner.top && r.right <= inner.right && r.bottom <= inner.bottom

    @Test fun rightOfTheMarkerWhenThereIsRoom() {
        val (i, r) = place(Offset(500f, 375f))
        assertEquals(0, i)
        assertEquals(500f + arm + 8f, r.left, 0.01f)
    }

    @Test fun flipsLeftAtTheRightEdge() {
        val (i, r) = place(Offset(960f, 375f))
        assertEquals(1, i)  // left
        assertTrue(inside(r))
        assertTrue(r.right <= 960f - arm)  // clear of the marker
    }

    @Test fun staysInsideInEveryCorner() {
        for (at in listOf(Offset(5f, 5f), Offset(995f, 5f), Offset(5f, 745f), Offset(995f, 745f))) {
            val (_, r) = place(at)
            assertTrue("label for $at at $r", inside(r))
        }
    }

    @Test fun keepsClearOfAnotherLabelAndAPanel() {
        val other = Rect(Offset(534f, 355f), size)  // where the first choice would go
        val panel = Rect(300f, 0f, 1000f, 750f)
        val (_, r1) = place(Offset(500f, 375f), listOf(other))
        assertEquals(0f, overlap(r1, other), 0.01f)
        val (_, r2) = place(Offset(250f, 375f), listOf(panel))
        assertEquals(0f, overlap(r2, panel), 0.01f)
        assertTrue(r2.right <= 300f)
    }

    @Test fun keepsItsLastPlaceWhileItStillWorks() {
        val (i, _) = place(Offset(500f, 375f), remembered = 3)  // down right, still clear
        assertEquals(3, i)
        // ...but not when that place now overlaps something.
        val (j, _) = place(Offset(500f, 375f), listOf(Rect(Offset(540f, 395f), Size(200f, 200f))), remembered = 3)
        assertTrue(j != 3)
    }

    @Test fun clampAndOverlap() {
        assertEquals(Rect(12f, 12f, 132f, 52f), clampInto(Rect(Offset(-50f, -50f), size), inner))
        assertEquals(100f, overlap(Rect(0f, 0f, 20f, 20f), Rect(10f, 10f, 30f, 30f)), 0.01f)
        assertEquals(0f, overlap(Rect(0f, 0f, 10f, 10f), Rect(10f, 0f, 20f, 10f)), 0.01f)
    }
}
