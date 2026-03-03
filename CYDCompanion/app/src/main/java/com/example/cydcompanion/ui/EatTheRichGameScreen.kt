package com.example.cydcompanion.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.imageResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import com.example.cydcompanion.R
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.min

private enum class Dir(val dx: Int, val dy: Int) {
    Up(0, -1), Down(0, 1), Left(-1, 0), Right(1, 0)
}

private data class Enemy(val x: Int, val y: Int, val color: Color, val label: String)

@Composable
fun EatTheRichGameScreen() {
    val map = remember {
        listOf(
            "###################",
            "#o.......#.......o#",
            "#.###.##.#.##.###.#",
            "#.....##.#.##.....#",
            "#.###.##.#.##.###.#",
            "#.................#",
            "#.###.#.#####.#.###",
            "#.....#...#...#...#",
            "#####.###.#.###.###",
            "#.......P.........#",
            "###.###.#.#.###.###",
            "#...#...#.#...#...#",
            "#.###.###.###.###.#",
            "#.................#",
            "#.###.##.#.##.###.#",
            "#o..#....#....#..o#",
            "###################"
        )
    }
    val rows = map.size
    val cols = map.first().length
    val pellets = remember {
        Array(rows) { y ->
            IntArray(cols) { x ->
                when (map[y][x]) {
                    '.' -> 1
                    'o', 'P' -> 2
                    else -> 0
                }
            }
        }
    }
    var pelletsLeft by remember { mutableIntStateOf(pellets.sumOf { row -> row.count { it != 0 } }) }

    val context = LocalContext.current
    val faceBitmap = remember(context) {
        ImageBitmap.imageResource(context.resources, R.drawable.player_face)
    }
    var score by remember { mutableIntStateOf(0) }
    var lives by remember { mutableIntStateOf(3) }
    var playerX by remember { mutableIntStateOf(9) }
    var playerY by remember { mutableIntStateOf(13) }
    var moveDir by remember { mutableStateOf(Dir.Left) }
    var queuedDir by remember { mutableStateOf(Dir.Left) }
    var powerUntilMs by remember { mutableLongStateOf(0L) }
    var gameOver by remember { mutableStateOf(false) }

    val enemyStart = remember {
        listOf(
            Enemy(9, 8, Color(0xFFE53935), "T"),
            Enemy(8, 9, Color(0xFF1E88E5), "M"),
            Enemy(10, 9, Color(0xFF8E24AA), "B"),
            Enemy(9, 10, Color(0xFFF57C00), "Z")
        )
    }
    val enemies = remember { androidx.compose.runtime.mutableStateListOf<Enemy>().apply { addAll(enemyStart) } }

    fun isWall(x: Int, y: Int): Boolean {
        val wx = (x + cols) % cols
        val wy = (y + rows) % rows
        return map[wy][wx] == '#'
    }

    fun step(x: Int, y: Int, dir: Dir): Pair<Int, Int> {
        val nx = (x + dir.dx + cols) % cols
        val ny = (y + dir.dy + rows) % rows
        return if (isWall(nx, ny)) x to y else nx to ny
    }

    fun resetPositions() {
        playerX = 9
        playerY = 13
        moveDir = Dir.Left
        queuedDir = Dir.Left
        enemies.clear()
        enemies.addAll(enemyStart)
    }

    LaunchedEffect(gameOver) {
        var tick = 0
        while (!gameOver) {
            delay(120)
            tick++
            val (qx, qy) = step(playerX, playerY, queuedDir)
            if (qx != playerX || qy != playerY) moveDir = queuedDir
            val (nx, ny) = step(playerX, playerY, moveDir)
            playerX = nx
            playerY = ny

            if (pellets[playerY][playerX] != 0) {
                if (pellets[playerY][playerX] == 2) {
                    powerUntilMs = System.currentTimeMillis() + 8_000L
                    score += 100
                } else {
                    score += 10
                }
                pellets[playerY][playerX] = 0
                pelletsLeft--
            }

            val frightened = System.currentTimeMillis() < powerUntilMs

            if (tick % 2 == 0) {
                for (i in enemies.indices) {
                    val e = enemies[i]
                    val options = Dir.entries.mapNotNull { d ->
                        val (ex, ey) = step(e.x, e.y, d)
                        if (ex == e.x && ey == e.y) null else Triple(d, ex, ey)
                    }
                    if (options.isNotEmpty()) {
                        val chosen = if (frightened) {
                            options.maxByOrNull { (_, ex, ey) -> abs(ex - playerX) + abs(ey - playerY) }
                        } else {
                            options.minByOrNull { (_, ex, ey) -> abs(ex - playerX) + abs(ey - playerY) }
                        }
                        if (chosen != null) enemies[i] = e.copy(x = chosen.second, y = chosen.third)
                    }
                }
            }

            for (i in enemies.indices) {
                val e = enemies[i]
                if (e.x == playerX && e.y == playerY) {
                    if (frightened) {
                        score += 250
                        enemies[i] = enemyStart[i]
                    } else {
                        lives--
                        if (lives <= 0) {
                            gameOver = true
                        } else {
                            resetPositions()
                        }
                    }
                }
            }
            if (pelletsLeft <= 0) gameOver = true
        }
    }

    var dragX by remember { mutableFloatStateOf(0f) }
    var dragY by remember { mutableFloatStateOf(0f) }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF050507))
            .pointerInput(Unit) {
                detectDragGestures(
                    onDragStart = {
                        dragX = 0f
                        dragY = 0f
                    },
                    onDragEnd = {
                        if (abs(dragX) > abs(dragY)) {
                            queuedDir = if (dragX >= 0f) Dir.Right else Dir.Left
                        } else {
                            queuedDir = if (dragY >= 0f) Dir.Down else Dir.Up
                        }
                    }
                ) { _, dragAmount ->
                    dragX += dragAmount.x
                    dragY += dragAmount.y
                }
            }
    ) {
        Column(modifier = Modifier.fillMaxSize()) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 14.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "SCORE $score",
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f)
                )
                val mode = if (System.currentTimeMillis() < powerUntilMs) "POWER" else "CHASE"
                Text(text = "LIVES $lives  $mode", color = Color(0xFFCFD8DC))
            }

            Canvas(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .padding(horizontal = 10.dp, vertical = 8.dp)
                    .background(Color.Black, RoundedCornerShape(12.dp))
                    .padding(8.dp)
            ) {
                val tile = min(size.width / cols, size.height / rows)
                val ox = (size.width - tile * cols) * 0.5f
                val oy = (size.height - tile * rows) * 0.5f
                val frightened = System.currentTimeMillis() < powerUntilMs
                for (y in 0 until rows) {
                    for (x in 0 until cols) {
                        val left = ox + x * tile
                        val top = oy + y * tile
                        val centerX = left + tile * 0.5f
                        val centerY = top + tile * 0.5f

                        if (map[y][x] == '#') {
                            drawRoundRect(
                                color = Color(0xFF22252D),
                                topLeft = androidx.compose.ui.geometry.Offset(left, top),
                                size = androidx.compose.ui.geometry.Size(tile, tile),
                                cornerRadius = androidx.compose.ui.geometry.CornerRadius(tile * 0.16f, tile * 0.16f)
                            )
                        } else {
                            if (pellets[y][x] == 1) {
                                drawCircle(Color.White, radius = tile * 0.09f, center = androidx.compose.ui.geometry.Offset(centerX, centerY))
                            }
                            if (pellets[y][x] == 2) {
                                drawCircle(Color(0xFFFFD54F), radius = tile * 0.19f, center = androidx.compose.ui.geometry.Offset(centerX, centerY))
                                drawCircle(Color(0xFFFFF176), radius = tile * 0.08f, center = androidx.compose.ui.geometry.Offset(centerX, centerY))
                            }
                        }
                    }
                }

                enemies.forEach { e ->
                    val cx = ox + e.x * tile + tile * 0.5f
                    val cy = oy + e.y * tile + tile * 0.5f
                    drawCircle(
                        color = if (frightened) Color(0xFF00BCD4) else e.color,
                        radius = tile * 0.42f,
                        center = androidx.compose.ui.geometry.Offset(cx, cy)
                    )
                    drawCircle(
                        color = Color.White.copy(alpha = 0.6f),
                        radius = tile * 0.42f,
                        center = androidx.compose.ui.geometry.Offset(cx, cy),
                        style = Stroke(width = tile * 0.05f)
                    )
                }

                val px = (ox + playerX * tile + tile * 0.08f).toInt()
                val py = (oy + playerY * tile + tile * 0.08f).toInt()
                val ps = (tile * 0.84f).toInt().coerceAtLeast(2)
                drawImage(
                    image = faceBitmap,
                    dstOffset = IntOffset(px, py),
                    dstSize = IntSize(ps, ps)
                )
                drawCircle(
                    color = Color.White.copy(alpha = 0.75f),
                    radius = tile * 0.42f,
                    center = androidx.compose.ui.geometry.Offset(ox + playerX * tile + tile * 0.5f, oy + playerY * tile + tile * 0.5f),
                    style = Stroke(width = tile * 0.04f)
                )
            }

            val footer = when {
                gameOver && pelletsLeft <= 0 -> "YOU WIN"
                gameOver -> "GAME OVER"
                else -> "SWIPE TO MOVE"
            }
            Text(
                text = footer,
                color = Color(0xFF90CAF9),
                fontWeight = FontWeight.Bold,
                modifier = Modifier
                    .align(Alignment.CenterHorizontally)
                    .padding(bottom = 12.dp)
            )
        }
    }
}
