# `.obj` — Object (Particle Type) Format Reference

**.obj files** define particle types — the fundamental game entity in Gusanos.
Bullets, blood, sparks, explosions, trails, and interactive objects are all `.obj` files.
They live under `default/objects/`. Parsed by `Goop/part_type.cpp`.

---

## Properties

### Colour & Rendering

| Property | Type | Default | Description |
|---|---|---|---|
| `colour` | `[R,G,B]` | `[255,255,255]` | Pixel colour for particles without a sprite. Each channel 0–255. |
| `color` | `[R,G,B]` | same | Alias for `colour` (AE spelling). US spelling wins if both given. |
| `alpha` | int | `255` | Global alpha 0–255. Values other than 255 force full Particle path (no SimpleParticle optimization). |
| `blender` | string | `none` | Blending mode for compositing. One of: `none`, `add`, `alpha`, `alphach`. |
| `invisible` | bool | `false` | When true, particle is never drawn. Useful for purely-interactive objects. |
| `render_layer` | int | `4` (WormRenderLayer) | Rendering layer in the object grid. Lower = drawn earlier (behind). |
| `wu_pixels` | bool | `false` | Use Wu anti-aliased pixel drawing. Only affects SimpleParticle path. |

### Sprite / Animation

| Property | Type | Default | Description |
|---|---|---|---|
| `sprite` | string | `""` | Filename of a PNG sprite sheet. Loaded relative to default/objects/. Multiple named frames extracted from rows, angled frames from columns. |
| `anim_duration` | int | `100` | Time (in ticks) for one full animation cycle. |
| `anim_type` | string | `loop_right` | Animation playback mode: `loop_right` (loop forward), `ping_pong` (forward-back), `right_once` (play once then hold last frame). |
| `anim_on_ground` | bool | `false` | Whether animation continues playing while on the ground. |

### Physics

| Property | Type | Default | Description |
|---|---|---|---|
| `gravity` | float | `0.0` | Vertical acceleration per tick. Positive = downward. |
| `bounce_factor` | float | `1.0` | Velocity multiplier on ground collision (0 = stop, 1 = full bounce). |
| `ground_friction` | float | `1.0` | Horizontal velocity multiplier per tick on ground. |
| `damping` | float | `1.0` | General velocity multiplier per tick (air resistance). |
| `acceleration` | float | `0.0` | Forward acceleration in the direction the particle is facing. |
| `max_speed` | float | `-1.0` | Speed cap (absolute). `-1` = no limit. |
| `angular_friction` | float | `0.0` | Angular velocity damping per tick (in degrees). |
| `repeat` | int | `1` | Number of particles emitted at spawn. High values create bursts. Forces full Particle path if > 1. |

### Collision / Interaction

| Property | Type | Default | Description |
|---|---|---|---|
| `radius` | float | `0.0` | Collision radius. 0 = treated as a point. |
| `health` | float | `100.0` | Hit points. Depleted by `damage()` actions. |
| `col_layer` | int | `0` | Collision layer in the object grid. `0` maps to `CustomColLayerStart` (the first custom collision layer). Negative values disable collisions. |
| `occluded` | bool | `false` | When true, the particle is culled (not rendered) when hidden behind terrain. |

### Line-to-Origin

| Property | Type | Default | Description |
|---|---|---|---|
| `line_to_origin` | bool | `false` | When true, a line is drawn from the particle's current position back to its spawn origin. Used for laser bolts, grappling hooks. |

### Distortion (visual warps)

| Property | Type | Default | Description |
|---|---|---|---|
| `distort_magnitude` | float | `1.0` | Strength of the visual distortion effect. |
| `distort_size` | `[W,H]` | `[0,0]` | Size of the distortion map buffer. |
| `distort_gen` | string | `""` | Name of a Lua function that generates the distortion map procedurally (analogous to `light_gen`). |

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

| Property | Type | Default | Description |
|---|---|---|---|
| `light_size` | `[W,H]` | `[0,0]` | Dimensions of the procedurally-generated light map. |
| `light_gen` | string | `""` | Lua function name that generates the light map. Called once at load with args (x, y, w, h). Return 0 for dark, 255 for fully lit. |
| `light_radius` | int | 0 | (Deprecated) Light radius. Overrides `light_gen`/`light_size` if set. Generates a simple radial gradient light. |

### Networking

| Property | Type | Default | Description |
|---|---|---|---|
| `sync_pos` | bool | `false` | Synchronize position over network. |
| `sync_spd` | bool | `false` | Synchronize velocity over network. |
| `sync_angle` | bool | `false` | Synchronize facing angle over network. |
| `network_init` | string | `""` | Inline Lua code (evaluated as a Lua expression to a function) run when the particle is created on a network node (`eZCom_EventInit`). Setting a non-empty value marks the particle as needing a network node (`needsNode`). |

---

## Event Handlers

Events are blocks starting with `on <name>(<params>)` followed by a newline and indented actions. Multiple handlers of the same event type can be defined (e.g. multiple `on timer()` blocks).

| Event | Parameters | Description |
|---|---|---|
| `creation()` | none | Fired once when the particle is spawned. Actions run immediately. |
| `death()` | none | Fired when the particle's health reaches 0. |
| `ground_collision()` | none | Fired when the particle hits solid terrain. |
| `timer(delay, delay_var, max_trigger)` | `delay`: ticks before first fire (default 100). `delay_var`: random variation added to delay (default 0). `max_trigger`: max number of times the timer fires; 0 = infinite (default 0). | Fires after a delay, and re-fires with the same delay each time (unless max_trigger limits it). |
| `detect_range(range, detect_owner, layers)` | `range`: detection radius in pixels. `detect_owner`: bool, whether to detect own player's objects (default true). `layers`: optional list of col_layer values to filter; `"worms"` matches worm layer. | Fires every tick while another object is within range. |
| `custom_event(index)` | `index`: integer event index (0–15) | Fired when the `custom_event(index)` action is invoked. Register at most one handler per index. |

---

## Action Commands (inside event handlers)

Actions are commands run when an event fires. Multiple actions can be listed, one per line.

| Command | Parameters | Description |
|---|---|---|
| `remove()` | none | Destroy this particle. |
| `damage(amount, amount_var, max_distance)` | `amount`: base damage. `amount_var`: random ± added. `max_distance`: max distance for damage falloff. | Deal damage to the detected/worm within range. |
| `shoot_particles(type, amount, speed, speed_var, motion_inheritance, amount_var, distribution, angle_offs, distance_offs)` | `type`: `.obj` name. `amount`: number to spawn. `speed`: base speed. `speed_var`: random ±. `motion_inheritance`: fraction of parent velocity inherited (0–1). `amount_var`: random ± count. `distribution`: angular spread. `angle_offs`: angle offset relative to parent facing. `distance_offs`: spawn distance from parent origin. | Spawn child particles. Named params can be used after positional (e.g. `distribution = 0`). |
| `uniform_shoot_particles(...)` | same as `shoot_particles` | Spawn child particles distributed uniformly in the spread angle. |
| `put_particle(type, x, y, xspd, yspd, angle)` | `type`: `.obj` name. `x`, `y`: world coordinates. `xspd`, `yspd`: initial velocity (default 0). `angle`: initial facing (default 0). | Spawn one particle at explicit coordinates with explicit velocity and angle. **Coordinate convention:** (x, y) is interpreted as the position of the sprite's **pivot point** as marked in the sprite image (defaults to the sprite's center). |
| `create_explosion(type)` | `type`: `.exp` name | Trigger an explosion effect at this particle's position. |
| `damp(factor)` | `factor`: velocity multiplier (0–1) | Reduce particle speed by multiplying velocity. |
| `repel(max_force, max_distance, min_force)` | `max_force`: force at distance 0. `max_distance`: range of effect. `min_force`: force at `max_distance` edge. | Apply a repulsion force away from detected object. |
| `push(factor)` | `factor`: push strength | Push the detected object. |
| `add_speed(amount, amount_var, offs, offs_var)` | `amount`: base speed to add. `amount_var`: random ±. `offs`: direction angle offset. `offs_var`: random ±. | Add velocity in a given direction. |
| `add_angle_speed(amount, amount_var)` | `amount`: angular velocity. `amount_var`: random ±. | Add rotational velocity. |
| `play_sound(sound, loudness, pitch, pitch_var)` | `sound`: filename or `[file1, file2, ...]`. `loudness`: volume. `pitch`: playback speed. `pitch_var`: random ±. | Play a sound at the particle's position (positional, distance-attenuated). |
| `play_sound_static(sound, loudness, pitch, pitch_var)` | same as `play_sound` | Play a sound at a fixed position (not attached to the particle's movement). |
| `play_global_sound(sound, volume, volume_var, pitch, pitch_var)` | same | Play a non-positional sound heard everywhere. |
| `apply_map_effect(effect)` | `effect`: `.mfx` filename | Apply a terrain effect (modify the destructible terrain bitmap). |
| `set_alpha_fade(frames, dest)` | `frames`: duration in ticks. `dest`: target alpha (0–255). | Smoothly fade the particle's alpha from current value to `dest` over `frames` ticks. |
| `show_firecone(sprite, frames, draw_distance)` | `sprite`: sprite filename. `frames`: number of frames. `draw_distance`: render distance. | Draw a muzzle-flash fire cone sprite at the particle's position (rendering only, no gameplay effect). |
| `custom_event(index)` | `index`: 0–15 | Fire the custom event handler registered at that index. |
| `run_script(code)` | `code`: Lua code (an expression evaluated to a function) | Compile and call the Lua function with the particle (`object`) and the detected object (`object2`, may be nil) as arguments. |

---

## Internal: SimpleParticle Optimization

When a `.obj` has minimal behavior, the engine auto-selects a lightweight `SimpleParticle` class instead of the full `Particle`. SimpleParticles have:

- Fixed-point position math
- No animation, sprite, distortion, or lighting
- Wu anti-aliasing variant when `wu_pixels = true`
- 32-bit and 16-bit colour depth variants

A particle type qualifies as "simple" only when **all** of the following hold (`part_type.cpp` `isSimpleParticleType()`):

- `repeat == 1` and `alpha == 255`
- No `sprite`, `blender`, `distortion`, or lighting (`lightHax` / `light_gen` / `light_radius`)
- `damping == 1.0` and `acceleration == 0.0`
- A `ground_collision()` handler **must** exist, and every action in it must be `remove()`
- At most one `timer()` handler (a single timer is allowed alongside the ground collision)
- No `death()` handler, no `line_to_origin`, no `detect_range`, and no networking (`needsNode`)

Any feature outside this set forces the full `Particle` path.

---

## Map Config (`config.cfg`)

A map's `config.cfg` declares map-level properties and event handlers. Currently recognized top-level flags:

| Flag | Type | Default | Description |
|---|---|---|---|
| `dark_mode` | bool | `false` | Enables lightmap-driven lighting on this map. |
| `spawnpoints` | list | `[]` | List of `[x, y]` or `[x, y, team]` entries for worm spawn positions. |

Event handlers:

| Event | Fires when |
|---|---|
| `on game_start()` | After the map is loaded, before the first worm spawns. |
| `on game_end()` | When the match ends. |

Example:
```
dark_mode = 1

spawnpoints = [
 [260,140],
 [212,454],
 [546,374],
 [566,144]
]

on game_start()
 put_particle(bunnyhopspawner1.obj, 510, 252)
 put_particle(bunnyhopspawner2.obj, 506, 267)
```

---

## Example

```
# A blood particle
gravity = 0.015
bounce_factor = 0
colour = [180, 0, 0]
render_layer = 8
col_layer = -1

on ground_collision()
 remove()
```