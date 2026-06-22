# `.wpn` — Weapon Type Format Reference

**.wpn files** define weapons — items that worms carry and fire. They live under
`default/weapons/`. Parsed by `Goop/weapon_type.cpp`.

---

## Properties

### Ammo & Reload

| Property | Type | Default | Description |
|---|---|---|---|
| `name` | string | `""` | Display name shown in the weapon HUD. |
| `ammo` | int | `1` | Number of shots before the weapon is depleted. |
| `reload_time` | int | `0` | Time (in ticks) between shots (reload delay). |
| `sync_shot` | bool | `false` | Whether shot timing is synced over the network (for delayed-fire weapons). |
| `sync_reload` | bool | `true` | Whether reload state is synced over the network. |

### Laser Sight

| Property | Type | Default | Description |
|---|---|---|---|
| `laser_sight_intensity` | float | `0.0` | Brightness of the laser sight beam. 0 = no laser sight rendered. |
| `laser_sight_range` | int | `-1` | Max range of the laser sight beam in pixels. `-1` = unlimited (goes until terrain hit). |
| `laser_sight_alpha` | int | `255` | Alpha of the laser sight beam (0–255). |
| `laser_sight_colour` | `[R,G,B]` | `[255,0,0]` | Colour of the laser sight beam. |
| `laser_sight_blender` | string | `none` | Blending mode for the laser sight beam: `none`, `add`, `alpha`. |

### Sprites

| Property | Type | Default | Description |
|---|---|---|---|
| `firecone` | string | `""` | Sprite shown at the weapon muzzle (firing arc). PNG filename. |
| `skin` | string | `""` | Sprite used as the weapon icon/skin. PNG filename. |

---

## Event Handlers

| Event | Parameters | Description |
|---|---|---|
| `primary_shoot()` | none | Fired when the player presses the primary fire key. Most weapons do their shooting here. |
| `primary_press()` | none | Fired on key-down (before shoot, for charge-up behavior). |
| `primary_release()` | none | Fired on key-release (for charge-release weapons). |
| `out_of_ammo()` | none | Fired when the weapon exhausts its ammo count. |
| `reload_end()` | none | Fired when the reload timer completes. |
| `timer(delay, delay_var, max_trigger, start_delay)` | `delay`: ticks between fires. `delay_var`: random ±. `max_trigger`: max times to fire (0 = infinite). `start_delay`: initial offset before first fire. | Fires repeatedly on a timer independent of weapon state. |
| `active_timer(delay, delay_var, max_trigger, start_delay)` | same as timer | Fires repeatedly only while the weapon is actively held/charged. |

---

## Action Commands (inside event handlers)

Same action system as `.obj` files. See [obj-format.md](obj-format.md) for the full list.

Weapon-specific patterns:

| Command | Parameters | Description |
|---|---|---|
| `use_ammo(count)` | `count`: ammo to consume (positive integer) | Deplete ammo. When ammo reaches 0, the `out_of_ammo` event fires. |
| `delay_fire(delay)` | `delay`: ticks to wait before firing | Stops the weapon from firing until `delay` ticks pass. Used for charge-up weapons. |
| `shoot_particles(type, count, inheritance, speed, speedVar, distribution, angleOffs, distOffs)` | see obj-format.md | Fires the actual projectile(s). Named params: `distribution = ...`, `distance_offs = ...`. |
| `play_sound(file, loudness, pitch, var)` | same as obj-format.md | Play a firing sound from the worm's position. |
| `play_global_sound(file, vol, volVar, pitch, pitchVar)` | same as obj-format.md | Play a non-positional weapon sound. |
| `add_speed(speed, var, angle, angleVar)` | same as obj-format.md | Apply recoil to the worm. |
| `show_firecone(frames, distance, spriteFile)` | `frames`: duration. `distance`: draw distance. `spriteFile`: optional override sprite. | Display the weapon's muzzle flash. |

---

## Example

```
# Laser weapon
name = LASER
ammo = 200
reload_time = 150

on primary_shoot()
 use_ammo(1)
 shoot_particles ( laser.obj , 1 , 1, 0, 0, 0, 0, 0, 5)
```