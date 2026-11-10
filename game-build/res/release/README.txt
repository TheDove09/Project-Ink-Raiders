Project: Ink Raiders
====================

A native Windows port of Splatoon Raiders (Nintendo Switch 2). This package contains NO game code or game assets:
you need your own copy of the game, dumped from your own console, with the 1.1.0 update.

Getting started
---------------
1. Start InkRaiders.exe. The start menu asks for your game: choose "Select game file" and pick your .nup.
   The game's code and data are copied into exefs\ and romfs\ next to the exe (about 15 GB). This takes a few minutes.
2. Play. Next time the game starts straight from those folders.

Already extracted the game yourself? Put the ExeFS in exefs\ (exefs\main must exist) and the RomFS in romfs\ next to the exe.

Requirements
------------
- Windows 10 or 11, 64-bit
- A GPU with Vulkan 1.3 (AMD, NVIDIA or Intel) and an up-to-date driver
- About 20 GB of free space

Tips
----
- In the game, set Options > Motion Controls to OFF: the right stick then turns the camera like a normal shooter.
- F1 (or View + Menu on a controller): settings - controls, camera sensitivity, display, sound.
- F11 or Alt+Enter: fullscreen.
- The first time you visit an area, its shaders are translated and saved; the first visit can stutter briefly, later ones do not.
- Problems? sra.log next to the exe says what happened.

Splatoon and Nintendo Switch 2 are trademarks of Nintendo. This project is not affiliated with or endorsed by Nintendo.
