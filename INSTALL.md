# Install 240-MP 

## On a Raspberry Pi

The following steps will set up an image for your Raspberry Pi with the latest version of 240-MP (and optionally set it up to autostart after boot)

### Requirements

- A RaspberryPi [4](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/) or [3B+](https://www.raspberrypi.com/products/raspberry-pi-3-model-b-plus/) - These are the only models I've tested with, it may work on others but sorry I can't say for sure
- SD Card (minimum of 4GB)
- A keyboard to navigate
- Internet Access (either WiFi or network cable will work)

### Optional

- A CRT TV and a composite cable - Composite out is my recommended way to use 240-MP but it will also work over HDMI as well so just select the config that works for your setup in step 2 below.  This is the composite cable I use if you happen to have a CRT: https://www.adafruit.com/product/2881
- USB remote control - Keyboard input works well but if you want that experience of sitting back and playing video on a VCR then a remote will definitely help with that.  I use this one: https://www.amazon.com/dp/B01FVUGPE8
- USB game controller - Most controllers (Xbox, PlayStation, 8BitDo, NES-style, etc.) should work out of the box: D-pad/left stick to navigate, A to select, B to go back, Start for play/pause.  Controllers can be plugged in at any time, and the button mapping can be customized — see [BUILDING.md → Gamepad input](BUILDING.md#gamepad-input-inputcfg).  If a controller isn't detected, make sure your user is in the `input` group: `sudo usermod -aG input $USER` then reboot.

### Steps

1) Write RaspberryPi OS Lite (64-bit) to an SD Card

    I reccomend using [Raspberry Pi Imager](https://www.raspberrypi.com/software/), it handles everything from OS selection to preconfiguring networking and user set up in nice simple flow

    Here is what you should select for OS if using Raspberry Pi Imager:

    | OS > Raspberry Pi OS (other) | Raspberry Pi OS Lite (64-bit) |
    | --- | --- |
    | <img src="https://github.com/user-attachments/assets/bb9f7a47-12b7-4580-abf4-ec8ad22153ba" /> | <img src="https://github.com/user-attachments/assets/30c39fce-99f8-48c9-9ad0-2b39b52690c1" /> |

2) After the write is complete, reconnect the card to your PC and update your boot/config.txt to one of the following (please make sure to choose the one that best matches your TV):

    **Option 1: For composite out on a CRT TV (NTSC)...**
    ```
    # --- Global ---

    arm_64bit=1
    disable_fw_kms_setup=1
    disable_splash=1
    disable_overscan=1
    dtparam=audio=on
    
    # Composite
    enable_tvout=1
    sdtv_mode=0
    sdtv_aspect=1
    
    # --- Pi 4B ---
    [pi4]
    
    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256
    dtoverlay=rpivid-v4l2
    
    # Overclocking
    over_voltage=2
    arm_freq=1750
    gpu_freq=600
    
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
    
    # --- Global ---
    [all]
    ```

    or **Option 2: for HDMI out on a modern TV...**
    ```
    # --- Global ---

    arm_64bit=1
    disable_fw_kms_setup=1
    disable_splash=1
    disable_overscan=1
    dtparam=audio=on

    # HDMI
    display_auto_detect=1
    hdmi_force_hotplug=1

    # --- Pi 4B ---
    [pi4]
    
    # Drivers & Video
    dtoverlay=vc4-fkms-v3d,cma-256
    dtoverlay=rpivid-v4l2
    
    # Overclocking
    over_voltage=2
    arm_freq=1750
    gpu_freq=600
    
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
    
    # --- Global ---
    [all]
    ```

3) Place the SD card in your Raspberry Pi and let it run through its first boot sequence

4) Once complete SSH in and run `sudo raspi-config`

    - Turn on Auto Login: `System Options > Auto Login > Yes`
    - Expand filesystem: `Advanced Options > Expand Filesystem > Yes`
    - Select Finish and allow the Raspberry Pi to reboot

5) After that completes SSH in again and run the following to install the latest version of 240-MP

    ```bash
    bash <(curl -fsSL https://github.com/anthonycaccese/240-mp/releases/latest/download/install.sh)
    ```

    This will install all of the needed dependencies (note: over WiFi it will take about 20 mins to complete) 

    You will get an option at the end of the install script that asks: `Install systemd autostart service? [y/N]` 

    If you type `Y` and press enter it will set up 240-MP to autostart when your Raspberry Pi boots which creates a nice appliance experience (bascially a dedicated 240-MP device).  

    If you choose that option please make sure to enter your primary user for the pi at the next prompt.  If you don't provide one it will set it up for the `Pi` user.

    At this point you can type `240mp` to start up the app.  If you chose to install the autostart service then the next time you boot your Pi it will boot directly into 240-MP.

### Update

To update to the latest release on Raspberry Pi please follow these steps (your settings will be retained):

1) SSH into your Raspberry Pi
2) Re-run the install script
    ```bash
    bash <(curl -fsSL https://github.com/anthonycaccese/240-mp/releases/latest/download/install.sh)
    ```
3) When it asks to `Install systemd autostart service? [y/N]`
    - if you already have autostart set up you can press either Y or N, it will keep your current autostart
    - if you don't have autostart installed and want to keep it that way just answer N
    - and if you want to turn on autostart just answer Y
4) Once that completes just run `sudo reboot` and when your pi restarts you'll be on the latest version

### Uninstall

1) If you'd like to remove 240-MP and continue to use your SD card for other things you can run the following commands via terminal or over SSH:

    ```bash
    sudo rm -rf /opt/240mp
    sudo rm /usr/local/bin/240mp
    ```

2) And if you installed the systemd autostart service then be sure to remove it by running the following commands:

    ```bash
    sudo systemctl unmask getty@tty1.service autovt@.service
    sudo systemctl disable 240mp.service
    sudo rm /etc/systemd/system/240mp.service
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

To update to the latest release on MacOS please follow these steps (your settings will be retained):
1. Download the DMG archive from the latest release
2. Mount it and move the 240mp.app into your Applications folder to overwrite your existing version. *Your existing configuration settings will be retained and it's safe to overwrite*

### Uninstall

- Remove it just like you would any application on macOS
- Remove the configuration files in `~/Library/Application Support/240-MP/`
