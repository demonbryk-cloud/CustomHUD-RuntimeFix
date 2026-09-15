# SpeedKMH

A small runtime fix for **ARCHIE's CustomHUD** that forces metric speed units (km/h) in supported HUD configurations.

## What does it do?

SpeedKMH makes CustomHUD use its built-in **metric unit path** instead of the imperial one.

This allows supported speedometers to:

- convert the displayed speed from mph to km/h;
- display the corresponding `km/h` unit label when the HUD provides it;
- keep the original vehicle physics unchanged;
- work without modifying the original `CustomHud.asi`.

The fix works at runtime and does not replace or modify CustomHUD itself.

### Example

Without SpeedKMH:

`129 mph`

With SpeedKMH:

`208 km/h`

The conversion is performed through CustomHUD's existing metric handling rather than by changing the vehicle's actual speed or physics.

## Installation

1. Download the latest `SpeedKMH.asi` from the **Releases** page.
2. Copy `SpeedKMH.asi` into the game's `scripts` folder.
3. Start the game.

Example:

```text
NFS Undercover/
└── scripts/
    ├── CustomHud.asi
    ├── SpeedKMH.asi
    └── ...
```
No changes to CustomHud.asi are required.

## Compatibility

SpeedKMH was tested with multiple CustomHUD speedometer styles and configurations.
Most tested HUDs worked correctly, including both official and community-made CustomHUD styles.

Some configurations could not be tested successfully because of game crashes or HUD loading problems. The exact cause of these cases is currently unknown and may be related to the HUD itself, the game, or compatibility with SpeedKMH.

## Known / unconfirmed issues
- NFS Underground 2 and GT4 HUD caused the game to crash during startup in the tested setup.
- NFS15-C originally required configuration changes to work correctly with its `digits.dds` based speedometer.

## Requirements
A supported game using ARCHIE's CustomHUD.
CustomHUD installed and working.
32-bit (x86) game environment.
Important

SpeedKMH does not include or redistribute CustomHUD.

You must obtain and install CustomHUD separately.

The original CustomHUD files and assets are not included in this project.

## Source

The source code is included in this repository.

SpeedKMH uses a runtime patch to make the relevant CustomHUD object report metric units.

It does not modify the original CustomHUD binary on disk.

## Credits
- **ARCHIE** — CustomHUD
- **OpenAI / ChatGPT** — assistance with reverse engineering, analysis and development of the SpeedKMH fix

This project is an unofficial community-made compatibility fix.

It is not affiliated with or endorsed by Electronic Arts, Criterion Games, or the author of CustomHUD.

Use at your own risk.
