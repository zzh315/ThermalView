package dev.thermalview

import androidx.compose.ui.geometry.Offset
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** The box (BoxEditor.kt): its edits in camera pixels and what a touch takes hold of. */
class BoxEditorTest {
    private val box = CamBox(64, 48, 128, 96)

    @Test fun movesInWholePixelsAndStaysInTheFrame() {
        assertEquals(CamBox(74, 43, 128, 96), box.edited(Grab.Move, 10.4f, -4.6f))
        assertEquals(CamBox(128, 96, 128, 96), box.edited(Grab.Move, 500f, 500f))
        assertEquals(CamBox(0, 0, 128, 96), box.edited(Grab.Move, -500f, -500f))
    }

    @Test fun resizesByEdgesAndCorners() {
        assertEquals(CamBox(54, 48, 138, 96), box.edited(Grab.Left, -10f, 99f))  // (dy ignored)
        assertEquals(CamBox(64, 48, 138, 106), box.edited(Grab.BottomRight, 10f, 10f))
        assertEquals(CamBox(64, 38, 128, 106), box.edited(Grab.Top, 0f, -10f))
    }

    @Test fun neverSmallerThanTheMinimumNorPastTheFrame() {
        val small = box.edited(Grab.Right, -1000f, 0f)
        assertEquals(CamBox.MIN, small.w)
        assertEquals(64, small.x)
        val tall = box.edited(Grab.Bottom, 0f, 1000f)
        assertEquals(192, tall.bottom)
        val shrunk = box.edited(Grab.TopLeft, 1000f, 1000f)
        assertEquals(CamBox.MIN, shrunk.w)
        assertEquals(CamBox.MIN, shrunk.h)
        assertEquals(box.right, shrunk.right)
    }

    // A box at 100..400 x 100..300 in a 1000 x 750 view; reach 56 px (28 dp at density 2).
    private fun hit(x: Float, y: Float, l: Float = 100f, t: Float = 100f, r: Float = 400f, b: Float = 300f) =
        hitBox(Offset(x, y), l, t, r, b, 56f, 1000f, 750f)

    @Test fun wellInsideMovesNearACornerOrEdgeResizes() {
        assertEquals(Grab.Move, hit(250f, 200f))
        assertEquals(Grab.TopLeft, hit(110f, 105f))
        assertEquals(Grab.BottomRight, hit(395f, 310f))
        assertEquals(Grab.Right, hit(405f, 200f))
        assertEquals(Grab.Top, hit(250f, 95f))
        assertNull(hit(700f, 600f))  // outside: zoom and pan
    }

    @Test fun aSmallBoxStillMovesFromItsMiddle() {
        assertEquals(Grab.Move, hit(250f, 200f, l = 230f, t = 185f, r = 270f, b = 215f))
    }

    @Test fun aBoxAroundTheWholeViewLetsDragsPan() {
        assertNull(hit(500f, 375f, l = -200f, t = -100f, r = 1300f, b = 900f))
        assertNull(hit(5f, 375f, l = -200f, t = -100f, r = 1300f, b = 900f))  // its edges are off screen
    }

    @Test fun aTurnedImageGrabsTheSideTheScreenShows() {
        // A quarter turn clockwise: the camera's left side is on top, its bottom on the left.
        assertEquals(Grab.Left, Grab.Top.onCamera(1))
        assertEquals(Grab.Bottom, Grab.Left.onCamera(1))
        assertEquals(Grab.BottomRight, Grab.BottomLeft.onCamera(1))
        // A half turn swaps opposite sides; three quarters: the camera's top on the left.
        assertEquals(Grab.TopRight, Grab.BottomLeft.onCamera(2))
        assertEquals(Grab.Top, Grab.Left.onCamera(3))
        for (rot in 0..3) assertEquals(Grab.Move, Grab.Move.onCamera(rot))
        for (g in Grab.entries) assertEquals(g, g.onCamera(0))
    }

    @Test fun onlyTheEdgesOnScreenCanBeGrabbed() {
        // Left edge on screen at x = 300, the others off it: the left edge, or a move from inside.
        assertEquals(Grab.Left, hit(310f, 375f, l = 300f, t = -100f, r = 1300f, b = 900f))
        assertEquals(Grab.Move, hit(600f, 375f, l = 300f, t = -100f, r = 1300f, b = 900f))
        assertNull(hit(998f, 375f, l = -200f, t = -100f, r = 1005f, b = 900f))  // its right edge is just off screen
    }
}
