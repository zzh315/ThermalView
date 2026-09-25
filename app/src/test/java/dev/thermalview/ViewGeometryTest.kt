package dev.thermalview

import org.junit.Assert.assertEquals
import org.junit.Test

/** The view's place and the camera <-> screen mapping (ViewGeometry.kt), for each of M6's turns. */
class ViewGeometryTest {
    @Test fun landscapeFitsFourByThree() {
        assertEquals(ViewBox(213f, 0f, 2133f, 1600f), viewBox(2560, 1600, 0))
        assertEquals(ViewBox(840f, 470f, 880f, 660f), viewBox(2560, 1600, 880))  // the Phone preset
    }

    @Test fun aQuarterTurnMakesItThreeByFour() {
        // Portrait, 1600 x 2560: the largest 3:4 fit, and a preset gives the long side.
        assertEquals(ViewBox(0f, 214f, 1600f, 2133f), viewBox(1600, 2560, 0, rot = 1))
        assertEquals(ViewBox(470f, 840f, 660f, 880f), viewBox(1600, 2560, 880, rot = 3))
        // (a half turn keeps it 4:3)
        assertEquals(viewBox(2560, 1600, 0), viewBox(2560, 1600, 0, rot = 2))
    }

    @Test fun placedAsGlViewportPlacesIt() {
        // 1001 px of height for 750: glViewport's y (from the bottom) rounds down, so the top rounds up.
        val b = viewBox(1000, 1001, 0)
        assertEquals(126f, b.y)
        assertEquals(750f, b.h)
    }

    @Test fun uvRoundTripsForEveryTurn() {
        for (rot in 0..3) {
            for ((s, t) in listOf(0.1f to 0.2f, 0.9f to 0.35f, 0.5f to 0.5f)) {
                val (a, b) = camUV(rot, s, t)
                val (s2, t2) = viewUV(rot, a, b)
                assertEquals("rot $rot", s, s2, 1e-6f)
                assertEquals("rot $rot", t, t2, 1e-6f)
            }
        }
    }

    @Test fun aQuarterTurnIsClockwise() {
        // The camera's top-left corner shows at the view's top right; its top right at the bottom right.
        assertEquals(1f to 0f, viewUV(1, 0f, 0f))
        assertEquals(1f to 1f, viewUV(1, 1f, 0f))
        assertEquals(0f to 0f, viewUV(1, 0f, 1f))
        // Three quarters: the top-left corner at the bottom left.
        assertEquals(0f to 1f, viewUV(3, 0f, 0f))
    }

    @Test fun cameraPointsLandWhereTheyShow() {
        val box = ViewBox(0f, 214f, 1600f, 2133f)
        val rect = CamRect(32f, 24f, 192f, 144f)  // 1.33x
        for (rot in 0..3) {
            val (sx, sy) = rect.toSurface(box, 100f, 50f, rot)
            val (cx, cy) = rect.toCamera(box, sx, sy, rot)
            assertEquals(100f, cx, 1e-3f)
            assertEquals(50f, cy, 1e-3f)
        }
        // A quarter turn: the frame's top edge runs down the view's right side.
        val (x, y) = CamRect(0f, 0f, 256f, 192f).toSurface(box, 128f, 0f, 1)
        assertEquals(1600f, x, 1e-3f)
        assertEquals(214f + 2133f / 2, y, 1e-3f)
    }

    @Test fun dragsAndPixelSizesTurnToo() {
        val box = ViewBox(0f, 0f, 750f, 1000f)
        val rect = CamRect(0f, 0f, 256f, 192f)
        // A quarter turn: a drag down the screen moves along the camera's rows (+x); right, up its columns (-y).
        assertEquals(256f to 0f, rect.cameraDelta(box, 0f, 1000f, 1))
        assertEquals(0f to -192f, rect.cameraDelta(box, 750f, 0f, 1))
        assertEquals(750f / 192f, rect.pixelSize(box, 1), 1e-4f)
        assertEquals(1000f / 256f, rect.pixelSize(box, 1), 1e-4f)
    }

    @Test fun aPinchKeepsTheCameraPointUnderTheFingers() {
        val box = ViewBox(0f, 0f, 750f, 1000f)
        val start = CamRect.of(2f, 128f, 96f)
        for (rot in 0..3) {
            val (px, py) = start.toCamera(box, 300f, 400f, rot)
            val next = start.transformed(box, 320f, 380f, 20f, -20f, 1.5f, rot)
            val (qx, qy) = next.toCamera(box, 320f, 380f, rot)
            assertEquals("rot $rot", px, qx, 1e-3f)
            assertEquals("rot $rot", py, qy, 1e-3f)
            assertEquals(3f, next.zoom, 1e-4f)
        }
    }

    @Test fun rectOnTheSurfaceIsOrdered() {
        val box = ViewBox(0f, 0f, 750f, 1000f)
        val r = CamRect(0f, 0f, 256f, 192f).toSurface(box, 64f, 48f, 192f, 144f, 1)
        assertEquals(187.5f, r.left, 1e-3f)
        assertEquals(250f, r.top, 1e-3f)
        assertEquals(562.5f, r.right, 1e-3f)
        assertEquals(750f, r.bottom, 1e-3f)
    }
}
