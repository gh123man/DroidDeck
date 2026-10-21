package com.droiddeck.launcher.glasses

import org.junit.Assert.*
import org.junit.Test

class XrealModelTest {
    @Test fun oneUses1080pNativeModeAndOwnOpticsWithoutChanging1s() {
        assertEquals("XREAL One", XrealModel.ONE.name)
        assertEquals(50f, XrealModel.ONE.diagonalFovDeg, 0f)
        assertEquals(2, XrealModel.ONE.mode(60, false))
        assertEquals(2, XrealModel.ONE.mode(0, false))
        assertEquals(5, XrealModel.ONE.mode(60, true))
        assertEquals(3, XrealModel.ONE.mode(90, false))
        assertEquals(4, XrealModel.ONE.mode(120, false))
        assertFalse(XrealModel.ONE.supportsDisplayMode(29))
        assertEquals("XREAL 1S", XrealModel.ONE_S.name)
        assertEquals(52f, XrealModel.ONE_S.diagonalFovDeg, 0f)
        assertEquals(29, XrealModel.ONE_S.mode(60, false))
        assertEquals(5, XrealModel.ONE_S.mode(60, true))
        assertTrue(XrealModel.ONE_S.supportsDisplayMode(29))
        for (model in listOf(XrealModel.ONE, XrealModel.ONE_S)) {
            assertFalse(model.supportsDisplayMode(0))
            assertFalse(model.supportsDisplayMode(6))
            assertFalse(model.sbs90)
            assertEquals(0, model.volumeMax)
        }
    }

    @Test fun oneDoesNotAdvertiseUnvalidatedBrightnessOrDimmingControls() {
        assertEquals(0, XrealModel.ONE.brightnessMax)
        assertFalse(XrealModel.ONE.hasSunblock)
        assertEquals(9, XrealModel.ONE_S.brightnessMax)
        assertTrue(XrealModel.ONE_S.hasSunblock)
    }
}
