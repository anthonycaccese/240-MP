# Install 240-MP

## On a Raspberry Pi

The following steps will set up an SD card for your Raspberry Pi with the latest version of 240-MP (and optionally set it up to autostart after boot).  

Steps 1-4 are focused on setting up a new card with Raspberry Pi OS Lite (64-Bit) and include options for writing a config.txt that will output to a CRT or modern TV.  

However, if you already have Raspberry Pi OS set up and working on your TV then the specific 240-MP install steps start at step 5.

### Requirements

- A [RaspberryPi 4](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/)
    - The Pi 4 fits in a nice sweet spot of performance + composite out and its the model I use daily so its the model I am most familiar with. It supports 1080p H264/HEVC playback well on both a CRT and over HDMI.
    - The [Pi 3B and 3B+](https://www.raspberrypi.com/products/raspberry-pi-3-model-b/) work well too with some caveats...  The default configuration for Pi 3 supports smooth 1080p H264 playback at the expense of inconsistent crop functionality during playback (cropping will display a black screen on some videos).  If crop is important for your use case on a Pi 3 then you can change the video decode settings with the caveat that 1080p H264 playback will no longer be smooth (720p and below  will still work well). The [hardware testing](https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing#raspberry-pi-3b) page has details on how to make that change.
    - If you choose to boot a Pi 3/3B+ from USB mass storage instead of SD, some USB flash drives can hang during early boot. If that happens, try an SD card or a different USB drive first before assuming the 240-MP install is the issue.
    - The [Pi 5](https://www.raspberrypi.com/products/raspberry-pi-5/) also works but I've only tested over HDMI to a modern TV. The Pi 5 doesn't have a direct composite output port, one can be added through a mod but I don't have the hardware to test that.
    - Full details on all models can be found on the [hardware testing](https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing) page on the wiki.  If you have a setup that is working for you and would like to help out others please add a comment to [this discussion](https://github.com/anthonycaccese/240-MP/discussions/44) so we can add it to the wiki.
- SD Card (minimum of 4GB) with RaspberryPi OS already set up
    - Note: 240-MP is only an application, it's not an OS so you will need to make sure you have an OS setup and working with the display you'd like to use. 
    - In the below steps I provide an example using Raspberry Pi OS Lite that you can use to create a fresh SD card along with configs I've tested for CRT and HDMI output.
    - The 240-MP specific steps start at step 5 so you can skip to that if you already have an OS running.
- A keyboard to navigate
- Internet Access (either WiFi or network cable will work)

### Optional

- A CRT TV and a composite cable
    - Composite out is my recommended way to use 240-MP
    - it will also work over HDMI as well so just select the config that works for your setup in step 2 below.
    - This is the composite cable I use if you happen to have a CRT: https://www.adafruit.com/product/2881 (note: I've only tested composite on the Pi 3/4 - the Pi 5 works well over HDMI
- USB remote control
    - Keyboard input works well but if you want that experience of sitting back and playing video on a VCR then a remote will definitely help with that.
    - I use this one: https://www.amazon.com/dp/B01FVUGPE8
- USB game controller 
    - Most controllers (Xbox, PlayStation, 8BitDo, NES-style, etc.) should work out of the box: D-pad/left stick to navigate, A to select, B to go back, Start for play/pause.  
    - Controllers can be plugged in at any time, and the button mapping can be customized — see [BUILDING.md → Gamepad input](BUILDING.md#gamepad-input-inputcfg).
    - If a controller isn't detected, make sure your user is in the `input` group: `sudo usermod -aG input $USER` then reboot.

### Steps

1) Write RaspberryPi OS Lite (64-bit) to an SD Card

    I reccomend using [Raspberry Pi Imager](https://www.raspberrypi.com/software/), it handles everything from OS selection to preconfiguring networking and user set up in nice simple flow

    Here is what you should select for OS if using Raspberry Pi Imager:

    | OS > Raspberry Pi OS (other) | Raspberry Pi OS Lite (64-bit) |
    | --- | --- |
    | <img src="https://github.com/user-attachments/assets/bb9f7a47-12b7-4580-abf4-ec8ad22153ba" /> | <img src="https://github.com/user-attachments/assets/30c39fce-99f8-48c9-9ad0-2b39b52690c1" /> |

    I would also suggest filling out Hostname, User and Wifi in the customization section and enabling SSH there as it will save you from having to set those up manually later.

2) After the write is complete, reconnect the card to your PC and update your config.txt to one of the following (please make sure to choose the one that best matches your TV):

    **Option 1: For composite out on a CRT TV (NTSC)...**
    ```
    # --- Global ---
    disable_splash=1
    disable_overscan=1
    dtparam=audio=on

    # Composite
    enable_tvout=1
    sdtv_mode=0      # 0 = NTSC, 2 = PAL
    sdtv_aspect=1    # 1 = 4:3

    # --- Pi 3B ---
    [pi3]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256

    # Overclocking
    over_voltage=4
    arm_freq=1300
    core_freq=450
    sdram_freq=500

    # --- Pi 3B+ ---
    [pi3+]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256

    # Overclocking
    over_voltage=2
    arm_freq=1500
    core_freq=500
    sdram_freq=500

    # --- Pi 4B ---
    [pi4]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256
    dtoverlay=rpivid-v4l2

    # Overclocking
    over_voltage=2
    arm_freq=1750
    gpu_freq=600

    # --- Pi 5 ---
    [pi5]

    # Drivers & Video
    dtoverlay=vc4-kms-v3d,cma-512,composite=1
    
    # --- Global ---
    [all]
    ```

    or **Option 2: for HDMI out on a modern TV...**
    ```
    # --- Global ---
    disable_splash=1
    disable_overscan=1

    # HDMI
    display_auto_detect=1
    hdmi_force_hotplug=1

    # --- Pi 3B ---
    [pi3]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256

    # Overclocking
    over_voltage=4
    arm_freq=1300
    core_freq=450
    sdram_freq=500

    # --- Pi 3B+ ---
    [pi3+]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256

    # Overclocking
    over_voltage=2
    arm_freq=1500
    core_freq=500
    sdram_freq=500

    # --- Pi 4B ---
    [pi4]

    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256
    dtoverlay=rpivid-v4l2

    # Overclocking
    over_voltage=2
    arm_freq=1750
    gpu_freq=600

    # --- Pi 5 ---
    [pi5]

    # Drivers & Video
    dtoverlay=vc4-kms-v3d,cma-512,composite=1
    
    # --- Global ---
    [all]
    ```

3) Place the SD card in your Raspberry Pi and let it run through its first boot sequence

    - Raspberry Pi OS Lite does **not** boot to a desktop. On first boot you may see boot log text, a text login console, or sometimes just a blinking cursor for a bit while it finishes booting.

4) Once complete, SSH in and run `sudo raspi-config`

    - If you set the hostname/user in Raspberry Pi Imager, SSH should usually be as simple as `ssh your-user@your-hostname.local`
    - If `.local` doesn't resolve on your network, use the Pi's IP address instead (`ssh your-user@192.168.x.x`). You can find it in your router's client list or on the Pi itself with `hostname -I`

    - Turn on Auto Login: `System Options > Auto Login > Yes`
    - Expand filesystem: `Advanced Options > Expand Filesystem > Yes`
    - Select Finish and allow the Raspberry Pi to reboot

5) After that completes SSH in again and run the following to install the latest version of 240-MP

    ```bash
    bash <(curl -fsSL https://github.com/anthonycaccese/240-mp/releases/latest/download/install.sh)
    ```

    This will install all of the needed dependencies (note: over WiFi it will take about 20 mins to complete) 

    **Optional** 
    - You will get an option at the end of the install script that asks: `Install systemd autostart service? [y/N]` 
    - If you type `Y` and press enter it will set up 240-MP to autostart when your Raspberry Pi boots which creates a nice appliance experience (bascially a dedicated 240-MP device).
    - If you choose that option please make sure to enter your primary user for the pi at the next prompt.  If you don't provide one it will set it up for the `Pi` user.
    - If you ever need to inspect the autostart logs later, use `sudo journalctl -u 240mp -f`

At this point you can type `240mp` at any time to start up the app.  And if you installed the autostart service then the next time you boot your Pi it will boot directly into 240-MP.

**If analog / composite audio is unusually quiet:** 
- Run `amixer sset PCM 100%`
- If that solves it and you want to keep the level across reboots, run `sudo alsactl store`

**Exit to Terminal:** 
- If you have the autostart service installed, the Quit dialog gains an `Exit to Terminal` option alongside `Power Off`. Choosing that will drop you to a login shell on the Pi instead of powering off, and leaves autostart intact for subsequent reboots. 
- To get back into 240-MP from that shell you can do one of the following:
    1. (*Recommended*) type `sudo systemctl start 240mp` to start up 240-MP and the autostart service again
    2. type `sudo reboot` to reboot and start up the device from scratch (which will also restart the autostart service)
    3. type `240mp` which will relaunch the app unmanaged in your shell; here the Quit dialog shows the plain Yes/No menu and selecting Yes will just return you to the shell rather than powering off

### Update

**From within the app (recommended):** go to `Settings → Update 240-MP`, check for updates, download, and choose Apply & Restart. Your settings will be retained.

> Installs made before in-app updates were possible need to re-run the install script **once** (just follow the steps below).  Doing that will pick up the new launcher.  There is a check on the Update screen that will let you know if that's the case for you.

**Via the install script** (also works any time; your settings will be retained):

1) SSH into your Raspberry Pi
2) Re-run the install script
    ```bash
    bash <(curl -fsSL https://github.com/anthonycaccese/240-mp/releases/latest/download/install.sh)
    ```
3) When it asks to "`Install systemd autostart service? [y/N]`"
    - If you already have autostart set up please answer `Y` (that's needed to keep the app files owned by the service user, which in-app updates rely on)
    - If you don't have autostart installed and want to keep it that way just answer `N`
    - And if you want to turn on autostart please answer `Y`
4) Once that completes just run `sudo reboot` and when your pi restarts you'll be on the latest version

### Uninstall

1) If you'd like to remove 240-MP and continue to use your SD card for other things then you can run the following commands via terminal or over SSH:

    ```bash
    sudo rm -rf /opt/240mp
    sudo rm /usr/local/bin/240mp
    ```

2) If you installed the autostart service and want to remove it then please run the running the following commands:

    ```bash
    sudo systemctl unmask getty@tty1.service autovt@.service
    sudo systemctl disable 240mp.service
    sudo rm -f /etc/systemd/system/240mp.service /etc/systemd/system/240mp-terminal.service /usr/local/bin/240mp-stop
    sudo systemctl daemon-reload
    ```

## On macOS (ARM)

If you don't have a Raspberry Pi and would like to try 240-MP, I also provide a build for macOS on Apple Silicon.  You can download a DMG archive from the latest release and run it on your mac following these steps...

### Requirements

- An Apple Silicon Mac running the latest version of macOS (it will not work on Intel based devices)
- Internet Access (either WiFi or network cable will work)

### Steps

1. Download the DMG archive from the latest release
2. Mount it and move the 240mp.app into your Applications folder
3. Make sure you have mpv installed (240-MP requires MPV for playback): `brew install mpv`
4. Double click 240-MP and it should open full screen

### Update

**From within the app (recommended):** go to `Settings → Update 240-MP`, check for updates, download, and choose Apply & Relaunch — the app will swap itself in `/Applications` and reopen on the new version. Your settings will be retained.

Or manually:
1. Download the DMG archive from the latest release
2. Mount it and move the 240mp.app into your Applications folder to overwrite your existing version. *Your existing settings will be retained and it's safe to overwrite*

### Uninstall

- Remove it just like you would any application on macOS
- Remove the configuration files in `~/Library/Application Support/240-MP/`
