# INAVR - Custom fork of Inav

This is an unofficial version of Inav, thus the added "R" to the end and also different versioning numbers.

I started this fork to be able to share with the rest of the community some changes and improvements which I have been using on my own planes and quads for quite some time. 

Most of the changes so far are focused on the DJI O3 OSD, which the official implementation has left a lot to be desired, and internal politics prevented most of these improvements from being merged into the main Inav code. 

Here's an example of the O3 OSD that's possible with this fork:
![O3_OSD_improved](InavR_O3_OSD_prev1.JPG)

These changes are added on top of the most recently released official version of Inav (7.1.1).

Compiled binaries from the code in this branch are available [in this Google Drive folder](https://drive.google.com/drive/folders/1ylWco9k0QQajGhIPQsC12iD4LPA3YMZ_?usp=sharing). Look under the v7.2.x sub-folder.

The software is still Inav through and through, so all the steps for flashing, configuring, tuning and adjusting are the same as the ones for the official version. 

To flash from the compiled binaries:
* Go into the firmware flasher screen of the Inav Configurator
* Click the "Load firmware [Local]" button (on the bottom right of the window)
* Select the .hex file you have downloaded to your computer (make sure you downloaded the correct target for your FC)
* Make sure "full chip erase" option is selected (on the top left of the window)
* Click "Flash firmware"

![LoadLocalFirmware](LoadLocalFirmware.png)

> The same diff-all method of transferring settings between flashes applies and can be used.

Below is the text from the official Inav readme.

# INAV - navigation capable flight controller

# F411 PSA

> INAV no longer accepts targets based on STM32 F411 MCU.

> INAV 7 is the last INAV official release available for F411 based flight controllers. The next milestone, INAV 8 will not be available for F411 boards.

![INAV](http://static.rcgroups.net/forums/attachments/6/1/0/3/7/6/a9088858-102-inav.png)

# PosHold, Navigation and RTH without compass PSA

Attention all drone pilots and enthusiasts,

Are you ready to take your flights to new heights with INAV 7.1? We've got some important information to share with you.

INAV 7.1 brings an exciting update to navigation capabilities. Now, you can soar through the skies, navigate waypoints, and even return to home without relying on a compass. Yes, you heard that right! But before you launch into the air, there's something crucial to consider.

While INAV 7.1 may not require a compass for basic navigation functions, we strongly advise you to install one for optimal flight performance. Here's why:

🛰️ Better Flight Precision: A compass provides essential data for accurate navigation, ensuring smoother and more precise flight paths.

🌐 Enhanced Reliability: With a compass onboard, your drone can maintain stability even in challenging environments, low speeds and strong wind.

🚀 Minimize Risks: Although INAV 7.1 can get you where you need to go without a compass, flying without one may result in a bumpier ride and increased risk of drift or inaccurate positioning.

Remember, safety and efficiency are paramount when operating drones. By installing a compass, you're not just enhancing your flight experience, but also prioritizing safety for yourself and those around you.

So, before you take off on your next adventure, make sure to equip your drone with a compass. It's the smart choice for smoother flights and better navigation.

Fly safe, fly smart with INAV 7.1 and a compass by your side!

# INAV Community

* [INAV Discord Server](https://discord.gg/peg2hhbYwN)
* [INAV Official on Facebook](https://www.facebook.com/groups/INAVOfficial)

## Features

* Runs on the most popular F4, F7 and H7 flight controllers
* MSP Displayport for all the HD Digital FPV systems: DJI, Walksnail and HDZero
* Outstanding performance out of the box
* Position Hold, Altitude Hold, Return To Home and Waypoint Missions
* Excellent support for fixed wing UAVs: airplanes, flying wings
* Blackbox flight recorder logging
* Advanced gyro filtering
* Fully configurable mixer that allows to run any hardware you want: multirotor, fixed wing, rovers, boats and other experimental devices
* Multiple sensor support: GPS, Pitot tube, sonar, lidar, temperature, ESC with BlHeli_32 telemetry
* Logic Conditions, Global Functions and Global Variables: you can program INAV with a GUI
* SmartAudio and IRC Tramp VTX support
* Telemetry: SmartPort, FPort, MAVlink, LTM, CRSF
* Multi-color RGB LED Strip support
* On Screen Display (OSD) - both character and pixel style
* And many more!

For a list of features, changes and some discussion please review consult the releases [page](https://github.com/iNavFlight/inav/releases) and the documentation.

## Tools

### INAV Configurator

Official tool for INAV can be downloaded [here](https://github.com/iNavFlight/inav-configurator/releases). It can be run on Windows, MacOS and Linux machines and standalone application.

### INAV Blackbox Explorer

Tool for Blackbox logs analysis is available [here](https://github.com/iNavFlight/blackbox-log-viewer/releases)

### INAV Blackbox Tools

Command line tools (`blackbox_decode`, `blackbox_render`) for Blackbox log conversion and analysis [here](https://github.com/iNavFlight/blackbox-tools).

### Telemetry screen for EdgeTX and OpenTX

Users of EdgeTX and OpenTX radios (Taranis, Horus, Jumper, Radiomaster, Nirvana) can use INAV OpenTX Telemetry Widget screen. Software and installation instruction are available here: [https://github.com/iNavFlight/OpenTX-Telemetry-Widget](https://github.com/iNavFlight/OpenTX-Telemetry-Widget)

### OSD layout Copy, Move, or Replace helper tool

[Easy INAV OSD switcher tool](https://www.mrd-rc.com/tutorials-tools-and-testing/useful-tools/inav-osd-switcher-tool/) allows you to easily switch your OSD layouts around in INAV. Choose the from and to OSD layouts, and the method of transfering the layouts.

## Installation

See: https://github.com/iNavFlight/inav/blob/master/docs/Installation.md

## Documentation, support and learning resources
* [INAV 5 on a flying wing full tutorial](https://www.youtube.com/playlist?list=PLOUQ8o2_nCLkZlulvqsX_vRMfXd5zM7Ha)
* [INAV on a multirotor drone tutorial](https://www.youtube.com/playlist?list=PLOUQ8o2_nCLkfcKsWobDLtBNIBzwlwRC8)
* [Fixed Wing Guide](docs/INAV_Fixed_Wing_Setup_Guide.pdf)
* [Autolaunch Guide](docs/INAV_Autolaunch.pdf)
* [Modes Guide](docs/INAV_Modes.pdf)
* [Wing Tuning Masterclass](docs/INAV_Wing_Tuning_Masterclass.pdf)
* [Official documentation](https://github.com/iNavFlight/inav/tree/master/docs)
* [Official Wiki](https://github.com/iNavFlight/inav/wiki)
* [Video series by Paweł Spychalski](https://www.youtube.com/playlist?list=PLOUQ8o2_nCLloACrA6f1_daCjhqY2x0fB)
* [Target documentation](https://github.com/iNavFlight/inav/tree/master/docs/boards)

## Contributing

Contributions are welcome and encouraged.  You can contribute in many ways:

* Documentation updates and corrections.
* How-To guides - received help?  help others!
* Bug fixes.
* New features.
* Telling us your ideas and suggestions.
* Buying your hardware from this [link](https://inavflight.com/shop/u/bg/)

A good place to start is the Discord channel, Telegram channel or Facebook group. Drop in, say hi.

Github issue tracker is a good place to search for existing issues or report a new bug/feature request:

https://github.com/iNavFlight/inav/issues

https://github.com/iNavFlight/inav-configurator/issues

Before creating new issues please check to see if there is an existing one, search first otherwise you waste peoples time when they could be coding instead!

## Developers

Please refer to the development section in the [docs/development](https://github.com/iNavFlight/inav/tree/master/docs/development) folder.

## INAV Releases
https://github.com/iNavFlight/inav/releases
