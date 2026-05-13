# game-and-watch-stage0

!!! VERY EARLY WIP !!!

Requires gnwmanager and openocd.

Copy Mario and Zelda firmware backups to `backups/`

```
make flash_mario_firmware
make flash_zelda_firmware
make flash_stage0
```

Press LEFT to start Mario firmware, RIGHT to start Zelda firmware.

Right now, you get a black screen and have to reset after switching firmwares, or even after quitting the current firmware.
Resetting the console from the original firmwares requires to hold POWER for ~5 seconds.

Stage0 is flashed at offset 0x20000 in both internal banks, to make sure it boots even if the banks are swapped (the console cold boots to 0x08020000).
Mario and Zelda firmwares are flashed each in an internal flash bank. The bank is swapped when starting either firmware.
The first 1MiB of extflash is re-programmed whenever you switch from Mario to Zelda (or the other way around).
