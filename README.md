# PebbleGotchi - A Tamagotchi P1 emulator for the Pebble smartwatches


## Synopsis

PebbleGotchi is a Tamagotchi P1 emulator for the Pebble smartwatches relying on the hardware agnostic Tamagotchi P1 emulation library [TamaLIB](https://github.com/jcrona/tamalib/).

![Pebble Time Steel](misc/screenshot.jpg)

For the time being, PebbleGotchi only supports the Pebble Time and the Pebble Time Steel.


## Build instruction

PebbleGotchi being an emulator, it requires a compatible Tamagotchi P1 ROM, which can be downloaded from [there](https://www.planetemu.net/rom/mame-roms/tama) for instance. You will allso need __TamaTool__, which can be downloaded or built from [there](https://github.com/jcrona/tamatool), to convert the binary ROM into a __.h__ file.

1. Clone __PebbleGotchi__ and its submodule:
```
$ git clone --recursive https://github.com/jcrona/pebblegotchi.git
```
2. Convert the ROM to __rom.h__ and place it in __pebblegotchi/src/c__:
```
$ tamatool -r rom.bin -H > pebblegotchi/src/c/rom.h
```
3. Build PebbleGotchi:
```
$ cd pebblegotchi
$ pebble build
```
4. Install the app:
```
$ pebble install --phone <your_phone_ip_address>
```
5. Try to keep your Tamagotchi alive !


## License

PebbleGotchi is distributed under the GPLv2 license. See the LICENSE file for more information.


## Hardware information

The Tamagotchi P1 is based on an E0C6S46 Epson MCU, and runs at 32,768 kHz. Its LCD is 32x16 B/W pixels, with 8 icons.
To my knowledge, the ROM available online has been extracted from a high-res picture of a die. The ROM mask was clear enough to be optically read. The pictures can be seen [there](https://siliconpr0n.org/map/bandai/tamagotchi-v1/) (thx asterick for the link !).  
I would love to see the same work done on a P2 and add support for it in TamaLIB/PebbleGotchi !

__  
Copyright (C) 2021 Jean-Christophe Rona
