# Android Mini-Game Drop-In (Pac-Man style)

This folder contains a self-contained Kotlin mini-game you can copy into your Android app.

## Included

- `src/main/java/com/example/eattherichgame/GameActivity.kt`
- `src/main/java/com/example/eattherichgame/EatTheRichGameView.kt`
- `src/main/java/com/example/eattherichgame/SecretGameUnlockController.kt`

## How to integrate

1. Copy the `src/main/java/com/example/eattherichgame` folder into your app module.
2. Add your player face image to `app/src/main/res/drawable/player_face.png`.
3. Register the activity in your app manifest:

```xml
<activity android:name=".eattherichgame.GameActivity" />
```

4. Launch it from anywhere in your app:

```kotlin
startActivity(Intent(this, GameActivity::class.java))
```

## Hidden unlock behind hyper-cube animation

Bind your hyper-cube view so 5 taps in 5 seconds opens the game:

```kotlin
import com.example.eattherichgame.enableSecretGameUnlock

// Example: in Activity.onCreate
val hyperCubeView = findViewById<View>(R.id.hyperCubeView)
hyperCubeView.enableSecretGameUnlock(
    context = this,
    requiredTaps = 5,
    windowMs = 5_000L
)
```

## Gameplay

- Swipe to move.
- Eat white pellets for points.
- Avoid billionaire caricature chasers.
- Eat gold power tokens to enter power mode and capture chasers.

## Notes

- The player sprite uses `R.drawable.player_face` (your uploaded face image).
- Chaser labels are configurable in `EatTheRichGameView.kt` (`enemyLabels`).
