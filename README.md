# Features & Development Status

## Building

Run `build.bat` to compile the project and generate the executable.

## Current Features

- Fully internal implementation using **ImGui**
- **Wall Checks**
  - Prevents the aimbot from attempting to target enemies through walls
- **Projectile Prediction**
  - Added support for non-hitscan weapons
  - Weapon-specific definitions still need to be configured
- **ESP**
- **Tracers**
- **Anti-Aim**
- **Silent Aim**
  - Currently requires additional fixes for multiplayer
  - Recommended for single-player use for now
- **PSilent**
  - Character model and camera remain visually unaffected while targeting
  - Currently host-only

## Known Limitations

- Scopes currently do not work correctly with the aimbot
- Silent Aim still needs a reliable multiplayer implementation
- Projectile prediction requires weapon-specific configuration

## Planned Features

- Projectile avoidance
- Item ESP
- Weak-point targeting, including head targeting
- General aimbot improvements
- Additional Anti-Aim modes
- Projectile aimbot support for Reptiloids

## Status

The project is actively being improved, with a focus on targeting behavior, projectile handling, ESP features, and overall reliability.
