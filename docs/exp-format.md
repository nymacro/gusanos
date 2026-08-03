# `.exp` — Explosion Type Format Reference

**.exp files** define explosion effects — short-lived visual+interactive events
at a fixed position. Explosions are attached to the terrain/clutter layer, not
to a moving particle. They live under `default/objects/`. Parsed by
`Goop/exp_type.cpp`.

---

## Properties

### Sprite & Rendering

| Property | Type | Default | Description |
|---|---|---|---|
| `sprite` | string | `""` | Filename of a PNG sprite sheet for the explosion. Loaded relative to default/objects/. |
| `invisible` | bool | `false` | When true, the explosion is not drawn. |
| `render_layer` | int | `4` (WormRenderLayer) | Rendering layer in the object grid. |
| `rock_hidden` | bool | `true` | Whether the explosion is hidden behind foreground terrain rocks. |
| `blender` | string | `none` | Blending mode: `none`, `add`, `alpha`, `alphach`. |
| `colour` | `[R,G,B]` | `[255,255,255]` | Base colour tint. |
| `alpha` | int | `255` | Global alpha 0–255. |
| `dest_alpha` | int | `-1` | Target alpha for fade-out. When >= 0, the explosion fades to this alpha over `timeout` ticks. |
| `wu_pixels` | bool | `false` | Use Wu anti-aliased pixel drawing. |

### Timing

| Property | Type | Default | Description |
|---|---|---|---|
| `timeout` | int | `0` | Lifetime in ticks. 0 = effectively infinite (removed only by actions). |
| `timeout_variation` | int | `0` | Random ± added to timeout. |

### Distortion

| Property | Type | Default | Description |
|---|---|---|---|
| `distort_magnitude` | float | `0.8` | Strength of the visual distortion effect. |

Distortion is set via the `distortion` function block:

```
distortion( lens <power> )
distortion( swirl <power> )
distortion( ripple <power> )
distortion( random <power> )
distortion( spin <power> )
distortion( bitmap <filename> )
```

### Lighting

Same as `.obj`. Light is generated via `light_size` + `light_gen`, or deprecated `light_radius`.

---

## Event Handlers

| Event | Parameters | Description |
|---|---|---|
| `creation()` | none | Fired once when the explosion spawns. |
| `detect_range(range, detect_owner, layers)` | `range`: radius. `detect_owner`: bool (default true). `layers`: optional list of col_layer values to filter; `"worms"` matches worm layer. | Fires every tick while an object is within range. |

---

## Action Commands (inside event handlers)

The same action system as `.obj` files. See [obj-format.md](obj-format.md) for the full list of commands.

Common patterns in `.exp` files:

*   `play_sound_static([file1.ogg, file2.ogg], loudness, pitch, pitch_var)` — play an explosion sound at the explosion's fixed position
*   `apply_map_effect(effect.mfx)` — carve a hole or scorch mark in the terrain
*   `damp(factor)` / `repel(maxForce, maxDist, minForce)` — push nearby objects away
*   `damage(amount, variation, maxDist)` — hurt worms/objects within range

---

## Example

```
# A standard explosion with sprite and sound
sprite = explosion1.png
blender = add
alpha = 255
dest_alpha = -1
timeout = 30

on creation()
 play_sound_static( [exp3.ogg, exp4.ogg], 14, 1, 0.05 )
 apply_map_effect( exp1hole.mfx )
```