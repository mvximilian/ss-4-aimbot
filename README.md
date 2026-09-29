# Serious Sam 4 — active wall checks

Extract the complete ZIP and run Start.cmd after loading gameplay. Close any other aim program first. Cheat Engine is not required. Insert shows/hides the menu; End or closing the menu exits and detaches. Restart this program after restarting the game.

## Internal ImGui menu

The default menu is now a **true injected DX11 ImGui renderer**. `Sam4-Aim.exe` still starts the controller and injects `Sam4-ImGui.dll` into `Sam4.exe`, but the DLL no longer creates a second overlay window or its own visible swap chain. Instead it hooks DXGI `Present` / `ResizeBuffers`, obtains the game's existing D3D11 device and back buffer, and renders ImGui directly in the game's frame. Mouse/keyboard input is handled by a temporary hook on the game window procedure while the menu is open.

Press **Insert** to show/hide the internal menu. Press **End** (or use the menu button) to stop the controller. Because this implementation renders through the game's D3D11 swap chain, **Serious Sam 4 must be running with its DirectX 11 renderer**. DX12/Vulkan need separate renderer hooks and are not handled by this DLL.

Build requirements: run `build.bat`. On the first build it downloads the pinned Dear ImGui **v1.92.9b** source and MinHook **v1.3.4**, then builds both `Sam4-Aim.exe` and `Sam4-ImGui.dll`. Keep the EXE and DLL in the same folder. The controller keeps the previous external UI only as a fallback if injection cannot be started; `Sam4-Aim.exe --external-ui` forces that fallback.

The existing ESP renderer is still external; this change specifically makes the **settings GUI** a true injected/internal ImGui menu.

## Controls

- Hold right mouse for normal aim.
- Silent aim: enable the checkbox, hold right mouse, and fire manually with left mouse. The firing rotation changes without camera-angle writes. There is no automatic firing.
- Wall check defaults on. It requests a fresh engine ray from the current weapon origin to the actual target point before applying aim. Blocked candidates are skipped in priority order. This replaces the old passive AI visibility cache.
- Priorities: Closest, FOV, Farthest, Lowest health.
- Aim FOV: 1–360 degrees; 360 includes enemies behind you.
- Range: 1–2,000 world units. ESP camera FOV defaults to 120 degrees.
- Beheaded enemies use a point 0.4 world units lower. The fresh ray includes that adjustment.
- Projectile prediction can lead moving targets using a configurable projectile speed (1-500 world units/second). Motion is estimated from recent target positions and smoothed to reduce jitter. Optional drop compensation (0-200 world units/second^2) raises the predicted aim point. Disable prediction for hitscan weapons. A yellow cross marks the predicted point on the overlay when it projects on-screen.
- ESP requires borderless/windowed mode. Boxes remain visible through walls. ESP stabilization is enabled by default and smooths small frame-to-frame projection jitter while snapping to genuine fast movement/camera cuts; disable it from the menu for raw projection.

The wall check rejects aiming when a fresh query fails or is blocked. It does not suppress your manual trigger input, change bullet penetration, or automatically detect projectile speeds. Prediction uses the speed/drop values configured in the menu. If no eligible target passes the check, your shot follows the game's normal crosshair direction.

## Third-person model options

Rotate model with silent aim: hold right mouse near a clear target with Silent aim enabled. The body-rotation hook turns the model without writing camera angles. Visual facing bridges visibility-query gaps for up to 500 ms; shots still require fresh checks. The user confirmed sustained model facing works.

Anti-aim: silently spins the player model while the game is focused. Spin speed adjusts live from 0 to 1,440 degrees per second (default 180); zero holds the current heading. Speed changes preserve the current rotation phase. It needs no target or mouse button and takes priority over model facing. Toggle it off to return to normal body control. Silent shot redirection remains a separate option. Anti-aim defaults off; its visual behavior still needs in-game confirmation. Use these options in third person; camera mode is not automatically detected.

## Implementation and limits

The standalone program uses the Windows debugger API, hardware breakpoints, and a small allocated call stub. It executes the engine ray query on the player's weapon thread, cleans up the query object, and restores the original register context. Game code is not patched. Ray queries add debugger overhead. A batch has a 20 ms scheduling budget; remaining candidates are checked on subsequent weapon callbacks.

Only one aim program can run at once. An older copy launched afterward may not honor this guard: use this package consistently. Engine offsets and collision filtering are specific to the tested game build. The engine visibility collision filter has been tested against a solid wall; every material, weapon, and penetration rule has not been verified. Weapons that bypass the observed firing routine may not support silent aim or active wall checks.

## Validation

- Active diagnostic: 58 clear and 106 blocked rays, zero setup errors. The same enemy produced both clear and blocked results in the user-controlled comparison.
- The earlier silent-aim build hit enemies without camera movement in the user's test.
- Integrated build: ranking, angle/quaternion math, FOV, projection, class filtering, freshness, live-cache regression, and UI tests pass. Build completed without warnings.
- The user confirmed that integrated active-wall silent aiming works. The user also confirmed sustained third-person model facing. Anti-aim is awaiting visual confirmation.

Diagnostics are saved to aim-diagnostics.txt beside the running program when launched through Start.cmd. Fresh ray counts and rotation-write counts show activity; write counts alone do not prove damage hits.

## Source

Build with Visual Studio C++ Build Tools using build.bat. Sources are main.cpp, ui.h, native_visibility.h, active_ray.h, model_facing.h, and priority.h. --self-test runs automated checks; --ui-test checks menu controls. --probe and --catalog read the game without aiming. --shot-monitor performs a bounded active-ray diagnostic without camera or shot-direction writes; it does allocate query state and execute engine query routines.

## Tracers

Enable Tracers to draw lines from screen bottom-center to enemies whose feet project onto the screen. Green lines indicate enemies; yellow marks the selected target. This toggle works independently of Box ESP, defaults off, and uses the same camera FOV and borderless/windowed overlay. Like boxes, lines can show through walls. Behind-camera and off-screen endpoints are omitted. Live visual alignment has not been verified for this addition.

### Wall-check camera aim fix
Wall Check now publishes successful fresh active-ray results into the normal visibility cache. With Silent Aim disabled, normal camera aim continues through the standard aim loop and is gated by that fresh visibility result. The selected clear target is also handed back to the main aim loop if the highest-priority candidate is blocked.

### True internal ImGui renderer
The injected DLL now renders ImGui from the game's own DX11 swap chain. It does not create a top-most helper window, so the previous black-screen/focus problem from the second-window design is removed.
