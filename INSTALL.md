# Install 240-MP 

## On a Raspberry Pi

The following steps will set up an SD card for your Raspberry Pi with the latest version of 240-MP (and optionally set it up to autostart after boot).  

Steps 1-4 are focused on setting up a new card with Raspberry Pi OS Lite (64-Bit) and include options for writing a config.txt that will output to a CRT or modern TV.  

However, if you already have Raspberry Pi OS set up and working on your TV then the specific 240-MP install steps start at step 5.

### Requirements

- A RaspberryPi [4](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/) or [3B+](https://www.raspberrypi.com/products/raspberry-pi-3-model-b-plus/).
    - These are the models I've tested with. The Pi 4 fits in a nice sweet spot of performance + composite out so that is the model I use daily.  
    The [5](https://www.raspberrypi.com/products/raspberry-pi-5/) also works but I've only tested **over HDMI** to a modern TV (the Pi 5 has no composite output port and I don't have the hardware to test direct video over HDMI to a CRT). 
    - It may work on other models too, but I'm sorry I can't say for sure.
    - I've started a [hardware testing](https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing) page on the wiki with more details.  If you have a setup that is working for you and would like to help out others please start [a discussion](https://github.com/anthonycaccese/240-MP/discussions/categories/q-a) so we can add it to the wiki.
- SD Card (minimum of 4GB) with RaspberryPi OS already set up
    - Note: 240-MP is only an application, it's not an OS so you will need to make sure you have an OS setup and working with the display you'd like to use. 
    - In the below steps I provide an example using Raspberry Pi OS Lite that you can use to create a fresh SD card along with configs I've tested for CRT and HDMI output.
    - The 240-MP specific steps start at step 5 so you can skip to that if you already have an OS running.
- A keyboard to navigate
- Internet Access (either WiFi or network cable will work)

### Optional

- A CRT TV and a composite cable - Composite out is my recommended way to use 240-MP but it will also work over HDMI as well so just select the config that works for your setup in step 2 below.  This is the composite cable I use if you happen to have a CRT: https://www.adafruit.com/product/2881 (note: I've only tested composite on the Pi 3/4 - the Pi 5 works well over HDMI)
- USB remote control - Keyboard input works well but if you want that experience of sitting back and playing video on a VCR then a remote will definitely help with that.  I use this one: https://www.amazon.com/dp/B01FVUGPE8
- USB game controller - Most controllers (Xbox, PlayStation, 8BitDo, NES-style, etc.) should work out of the box: D-pad/left stick to navigate, A to select, B to go back, Start for play/pause.  Controllers can be plugged in at any time, and the button mapping can be customized — see [BUILDING.md → Gamepad input](BUILDING.md#gamepad-input-inputcfg).  If a controller isn't detected, make sure your user is in the `input` group: `sudo usermod -aG input $USER` then reboot.

### Steps

1) Write RaspberryPi OS Lite (64-bit) to an SD Card

    I reccomend using [Raspberry Pi Imager](https://www.raspberrypi.com/software/), it handles everything from OS selection to preconfiguring networking and user set up in nice simple flow

    Here is what you should select for OS if using Raspberry Pi Imager:

    | OS > Raspberry Pi OS (other) | Raspberry Pi OS Lite (64-bit) |
    | --- | --- |
    | <img src="https://github.com/user-attachments/assets/bb9f7a47-12b7-4580-abf4-ec8ad22153ba" /> | <img src="https://github.com/user-attachments/assets/30c39fce-99f8-48c9-9ad0-2b39b52690c1" /> |

    I would also suggest filling out Hostname, User and Wifi in the customization section as it will you save you from having to set them up manually later.

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
    
    # --- Pi 5 ---
    [pi5]
    
    # Drivers & Video (Pi 5 requires full KMS — fkms is not supported, and
    # there is no hardware H.264 block so video uses fast software decode)
    dtoverlay=vc4-kms-v3d
    
    # --- Global ---
    [all]
    ```

3) Place the SD card in your Raspberry Pi and let it run through its first boot sequence

4) Once complete, SSH in and run `sudo raspi-config`

    - Turn on Auto Login: `System Options > Auto Login > Yes`
    - Expand filesystem: `Advanced Options > Expand Filesystem > Yes`
    - Select Finish and allow the Raspberry Pi to reboot

5) After that completes SSH in again and run the following to install the latest version of 240-MP

    ```bash
    bash <(curl -fsSL https://github.com/anthonycaccese/240-mp/releases/latest/download/install.sh)
    ```

    This will install all of the needed dependencies (note: over WiFi it will take about 20 mins to complete) 

    **[Optional]** 
    - You will get an option at the end of the install script that asks: `Install systemd autostart service? [y/N]` 
    - If you type `Y` and press enter it will set up 240-MP to autostart when your Raspberry Pi boots which creates a nice appliance experience (bascially a dedicated 240-MP device).
    - If you choose that option please make sure to enter your primary user for the pi at the next prompt.  If you don't provide one it will set it up for the `Pi` user.

    At this point you can type `240mp` at any time to start up the app.  And if you installed the autostart service then the next time you boot your Pi it will boot directly into 240-MP.

### Update

To update to the latest release on Raspberry Pi please follow these steps (your settings will be retained):

1) SSH into your Raspberry Pi
2) Re-run the install script
    ```bash
    bash <(curl -fsSL https://github.com/anthonycaccese/240-mp/releases/latest/download/install.sh)
    ```
3) When it asks to "`Install systemd autostart service? [y/N]`"
    - If you already have autostart set up you can press either Y or N, it will keep your current autostart
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
