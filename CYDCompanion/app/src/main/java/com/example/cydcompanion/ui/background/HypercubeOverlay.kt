package com.example.cydcompanion.ui.background

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.State
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import kotlin.math.cos
import kotlin.math.sin

private data class Vec4(val x: Float, val y: Float, val z: Float, val w: Float)
private data class Vec3(val x: Float, val y: Float, val z: Float)
private data class Vec2(val x: Float, val y: Float)

@Composable
fun HypercubeOverlay(modifier: Modifier = Modifier) {
    val phase by rememberNeonPhase()
    val vertices = remember { buildHypercubeVertices() }
    val edges = remember { buildHypercubeEdges() }

    Canvas(modifier = modifier) {
        drawHypercube(
            t = phase,
            vertices = vertices,
            edges = edges
        )
    }
}

@Composable
private fun rememberNeonPhase(): State<Float> {
    val state = remember { mutableFloatStateOf(0f) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(16)
            state.floatValue += 0.016f
        }
    }
    return state
}

private fun DrawScope.drawHypercube(t: Float, vertices: List<Vec4>, edges: List<Pair<Int, Int>>) {
    val driftX = sin(t * 0.41f) * size.minDimension * 0.03f
    val driftY = cos(t * 0.33f) * size.minDimension * 0.025f
    val pulse = 1f + (sin(t * 0.8f) * 0.06f)
    val center = Offset(
        x = size.width * 0.5f + driftX,
        y = size.height * 0.5f + driftY
    )
    val baseSize = size.minDimension * 0.38f * pulse
    val projected = vertices.map { v4 ->
        val r4 = rotate4d(v4, t)
        val p3 = project4dto3d(r4)
        val r3 = rotate3d(p3, t)
        val p2 = project3dto2d(r3)
        p2
    }
    val points = projected.map { p ->
        Offset(center.x + p.x * baseSize, center.y + p.y * baseSize)
    }
    val glowColor = Color(0x4400E5FF)
    val edgeColor = Color(0xB300E5FF)
    val innerColor = Color(0x7AFF2BD6)
    val glowStroke = 2.0.dp.toPx()
    val edgeStroke = 0.95.dp.toPx()
    val secondaryStroke = 0.55.dp.toPx()

    edges.forEach { (a, b) ->
        drawLine(
            color = glowColor,
            start = points[a],
            end = points[b],
            strokeWidth = glowStroke,
            cap = StrokeCap.Round
        )
    }
    edges.forEach { (a, b) ->
        drawLine(
            color = edgeColor,
            start = points[a],
            end = points[b],
            strokeWidth = edgeStroke,
            cap = StrokeCap.Round
        )
    }
    edges.forEach { (a, b) ->
        drawLine(
            color = Color(0x66FF2BD6),
            start = points[a],
            end = points[b],
            strokeWidth = secondaryStroke,
            cap = StrokeCap.Round
        )
    }
    points.forEach { p ->
        drawCircle(color = innerColor, radius = 1.9.dp.toPx(), center = p)
        drawCircle(
            color = Color(0xAAFFFFFF),
            radius = 0.8.dp.toPx(),
            center = p,
            style = Stroke(width = 0.6.dp.toPx())
        )
    }
}

private fun buildHypercubeVertices(): List<Vec4> {
    val v = ArrayList<Vec4>(16)
    for (i in 0 until 16) {
        val x = if ((i and 0b0001) == 0) -1f else 1f
        val y = if ((i and 0b0010) == 0) -1f else 1f
        val z = if ((i and 0b0100) == 0) -1f else 1f
        val w = if ((i and 0b1000) == 0) -1f else 1f
        v.add(Vec4(x, y, z, w))
    }
    return v
}

private fun buildHypercubeEdges(): List<Pair<Int, Int>> {
    val e = ArrayList<Pair<Int, Int>>(32)
    for (i in 0 until 16) {
        for (bit in 0..3) {
            val j = i xor (1 shl bit)
            if (i < j) e.add(i to j)
        }
    }
    return e
}

private fun rotate4d(v: Vec4, t: Float): Vec4 {
    var x = v.x
    var y = v.y
    var z = v.z
    var w = v.w
    val a = t * 0.9f
    val b = t * 0.7f
    val c = t * 0.6f
    var cA = cos(a)
    var sA = sin(a)
    var nx = x * cA - w * sA
    var nw = x * sA + w * cA
    x = nx
    w = nw
    cA = cos(b)
    sA = sin(b)
    val ny = y * cA - z * sA
    val nz = y * sA + z * cA
    y = ny
    z = nz
    cA = cos(c)
    sA = sin(c)
    nx = x * cA - z * sA
    val nz2 = x * sA + z * cA
    x = nx
    z = nz2
    return Vec4(x, y, z, w)
}

private fun project4dto3d(v: Vec4): Vec3 {
    val d = 2.6f
    val k = d / (d - v.w * 0.65f)
    return Vec3(v.x * k, v.y * k, v.z * k)
}

private fun rotate3d(v: Vec3, t: Float): Vec3 {
    val ay = t * 0.55f
    val az = t * 0.35f
    val cy = cos(ay)
    val sy = sin(ay)
    val cz = cos(az)
    val sz = sin(az)
    val x1 = v.x * cy + v.z * sy
    val z1 = -v.x * sy + v.z * cy
    val x2 = x1 * cz - v.y * sz
    val y2 = x1 * sz + v.y * cz
    return Vec3(x2, y2, z1)
}

private fun project3dto2d(v: Vec3): Vec2 {
    val d = 5f
    val k = d / (d - v.z)
    return Vec2(v.x * k, v.y * k)
}
