# obs-retroize

An OBS Studio video filter that pixelates and color-quantizes any source to look
like retro game console output.

## Presets

| Preset | Resolution | Color depth |
|---|---|---|
| 8-bit (NES) | 256x240 | 54-color 2C02 palette |
| 16-bit (SNES) | 256x224 | 15-bit RGB (32,768 colors) |
| 16-bit (Genesis) | 320x224 | 9-bit RGB (512 colors) |
| 4-shade (Game Boy) | 160x144 | 4-shade DMG green palette |
| Custom | user-defined | 1-8 bits per channel |

Block size is computed per frame so the source's aspect ratio is preserved.

## Apply

In OBS: right-click any source -> Filters -> "+" under Effect Filters -> Retroize.

## Build

Uses the official `obs-plugintemplate` toolchain. See the template
[Quick Start Guide](https://github.com/obsproject/obs-plugintemplate/wiki/Quick-Start-Guide).

## License

BSD 2-Clause. See `LICENSE`.
