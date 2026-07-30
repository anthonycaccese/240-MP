<img src="https://github.com/user-attachments/assets/73c3e46f-a74a-4d96-9c4f-ae30f28378be" />

# 240-MP

240-MP is a retro VCR style frontend to play content on [Raspberry Pi](https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing) (preferably hooked up to a CRT TV), Steam OS (and other Linux x86_64 distros) or MacOS (ARM).

Playback experiences are handled via modules to enable new integrations without requiring major changes to the overall frontend. Try to think of each module as a different input on a VHS deck. There are 8 included modules currently: [Local Files](https://github.com/anthonycaccese/240-MP/wiki/Module:-Local-Files), [Plex](https://github.com/anthonycaccese/240-MP/wiki/Module:-Plex), [Jellyfin](https://github.com/anthonycaccese/240-MP/wiki/Module:-Jellyfin), Emby, [YouTube](https://github.com/anthonycaccese/240-MP/wiki/Module:-YouTube), [NFC Reader](https://github.com/anthonycaccese/240-MP/wiki/Module:-NFC-Reader), [Weather](https://github.com/anthonycaccese/240-MP/wiki/Module:-Weather) and a module similar to art/wallpaper modes on modern tvs called [Ambient:Mode](https://github.com/anthonycaccese/240-MP/wiki/Module:-Ambient-Mode).

It's built to work in conjuction with MPV which will be installed (or updated) as a dependency during the [install](#Install) steps.  Some modules (like YouTube and NFC Reader) have additional dependencies which are covered on their associated wiki pages under the "To Enable" section.

## Video Overview

Watch on YouTube: https://youtu.be/r-gylGDoELY

## Photos

| Module Selection | Item Detail |
| --- | --- |
| <img src="https://github.com/user-attachments/assets/9472d55a-4617-4a7f-80c4-32aa28494048" /> | <img src="https://github.com/user-attachments/assets/4f7d8230-860a-4ace-9370-9f59f43289c0" /> |

| Resume Option | Playback | Settings |
| --- | --- | --- |
| <img src="https://github.com/user-attachments/assets/490e9ebd-fab2-4fd1-9959-35ebb619eff0" /> | <img src="https://github.com/user-attachments/assets/a3c768c7-6ede-4cdf-9d03-90aee7b8cdfb" /> | <img src="https://github.com/user-attachments/assets/0fd48977-8776-4334-b34e-d12256f23b97" /> |

## Modules

### Ambient:Mode ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Ambient-Mode))
- Supported video file types: `"mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "flv", "f4v", "mpg", "mpeg", "vob"`
- Playlist support for audio tracks using `m3u` and `m3u8` files
- Mix video with a different audio track
- Loops forever until you stop it

### Emby Module ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Emby))
- Supported library types: `movies, tvshows, homevideos, boxsets`
- Username / password authentication, or Emby Connect (emby.media cloud account, with server picker)
- Select specific libraries to display
- Continue Watching, Next Up and Resume Playback
- Autoplay next episode in a season (optional, off by default)
- Intro/Credit skip using the server's chapter markers (when detected)
- Collections support
- Select preferred audio/subtitle track before playback and switch tracks during playback
- Full library browsing by letter
- Show/Season browsing
- Video quality selection: Direct Playback (Default) or Transcode options

### Jellyfin ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Jellyfin))
- Supported library types: `movies, tvshows, homevideos, boxsets`
- "Quick Connect" authentication
- Select specific libraries to display
- Continue Watching, Next Up and Resume Playback
- Autoplay next episode in a season (optional, off by default)
- Collections support
- Select preferred audio/subtitle track before playback and switch tracks during playback
- Full library browsing by letter
- Show/Season browsing
- Video quality selection: Direct Playback (Default) or Transcode options

### Local Files ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Local-Files))
- Supported file types: `"mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "flv", "f4v", "mpg", "mpeg", "vob"`
- Playlist support using `m3u` and `m3u8` files
- Folder browsing
- Loop playback
- Shuffle playback
- Playback history
- Switch audio/subtitle tracks during playback

### NFC Reader ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-NFC-Reader))
- Start video playback via NFC cards
- Tested reader support: `ACS ACR122U`
- Maps cards to videos via per-card text files in a `nfc_tags` data directory
- Tapping an unknown card auto-creates a stub tag file for it

### Plex ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Plex))
- Supported library types: `Movies, TV Shows, Other Videos`
- Server switching
- User profile switching and auto sign in
- Select specific libraries to display
- Continue Watching and Resume
- Autoplay next episode in a season (optional, off by default)
- Hub, Playlist, Collection and Category support
- Movie editions
- Select preferred audio/subtitle track before playback and switch tracks during playback
- Full library browsing by letter
- Show/Season browsing
- Video quality selection: Direct Playback (Default) or Transcode options

### Weather ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Weather))
- Inspired by [WeatherStar 3000+](https://github.com/netbymatt/ws3kp) by netbymatt
- Integrates with Open-Meteo to provide weather forecasts for worldwide locations
- Integrates with NWS to provide current conditions for US locations
- For your main location it displays Current Conditions and Extended (3-day forecast)
- Can display forecast data for 6 additional locations
- Supports background music, US/Metric Units and 12-hour/24-hour time display

### YouTube ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-YouTube))
- List content from YouTube RSS feeds and playback via mpv + yt-dl (no auth required)
- View Subscriptions: Browse the latest videos from your configured channels as a reverse chronological list
- Browse videos by Channel
- Save to a local Watch Later list
- View your local Watch History
- Resume Playback
- Set Playback Resolution: 480p (default and good for the RaspberryPi), 720p and 1080p
- Choose to Display Shorts or not (default is On)

## Install
- [On a Raspberry Pi](INSTALL.md#on-a-raspberry-pi)
- [On macOS (ARM)](INSTALL.md#on-macos-arm)
- [On SteamOS / Linux x86_64](INSTALL.md#on-steamos--linux-x86_64)

## Hardware Testing
- [Raspberry Pi 3B](https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing#raspberry-pi-3b)
- [Raspberry Pi 3B+](https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing#raspberry-pi-3b-1)
- [Raspberry Pi 4B](https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing#raspberry-pi-4b)
- [Raspberry Pi 5](https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing#raspberry-pi-5)
- [Steam Deck](https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing#steam-deck)

## FAQs

- Why didn't you use Kodi/LibreELEC/OSMC?
    - I've used all of those distros and they are all excellent but I also like making things and wanted something simpler without as many options.  Something that felt like a VCR from my youth.
- Should I use 240-MP instead of Kodi/LibreELEC/OSMC?
    - I would recommend thinking about it like this...
    - All of those distros are amazing, feature rich, work across a ton of devices and have awesome supportive teams behind them.
    - I on the other hand am just one person making nostalgic things for my own niche use cases.
    - If those use cases match with what you're looking for, then 240-MP is a bunch of fun and I'd be happy for you to try it.
    - Otherwise, the well known distros are spectacular and you should likely open those doors instead.
- Will this work on other Raspberry Pi models? (like the 5, 2 zero, etc...)
    - I've tested on the 4b, 3b+ and 3b. Other users have confimred the 5 works well too and all the details on what we've confimred can be found here: https://github.com/anthonycaccese/240-MP/wiki/Hardware-Testing
    - If its not on that list then the short answer is "we don't know but please feel free try and let us know if it works"
- Where does the name "240-MP" come from?
    - 240 has a double meaning referring to the longest [VHS tape length](https://en.wikipedia.org/wiki/VHS#Tape_lengths) and love for [CRT TVs](https://consolemods.org/wiki/CRT:What_is_240p%3F) as a display type.
    - MP also has a double meaning of "Media Player" and a play on the "SP/LP/EP/SLP" terminology that was used to refer to the recording quality for VHS recordings.
- Does the 240 in the name mean that it outputs at 240p resolution?
    - The UI scales based on the OS config and output cables you are using.
    - For example: the output resolution for the menu and video playback when using it on a CRT with the configs I use is 480i/576i
- Does 240-MP support RGB out instead of composite?
    - 240-MP is just an app that runs on top of an already configured Operating System. If you are able to configure your OS on the Raspberry Pi to output over RGB then 240-MP will simply scale and display to that output when it boots up as well.
    - If you have a combination of RGB out + OS configuration that works well then please add a comment here with your set up details: https://github.com/anthonycaccese/240-MP/discussions/44
- Does 240-MP work over HDMI on a modern television too?
    - Yes! The UI was built to scale on modern televisions over HDMI as well.
    - Please make sure you use the config.txt I provide for HDMI and it will output at the proper resolution for a modern tv.
- Does 240-MP support bluetooth keyboards/remotes/controllers?
    - 240-MP is just an app that runs on top of an already configured Operating System. If your OS has a way to configure and set up bluetooh controllers then 240-MP will simply see them as controllers when it boots up.

## Credits & Acknowledgments

- The `VCR OSD Mono` font was created by Riciery Santos Leal (a.k.a. mrmanet) https://www.dafont.com/vcr-osd-mono.font
- The `Unifont` font (used as a fallback for characters that VCR OSD Mono does not cover) is GNU Unifont by Roman Czyborra, Paul Hardy, et al., licensed under the SIL Open Font License v1.1. https://unifoundry.com/unifont/ — license text: [assets/fonts/LICENSE-unifont.txt](assets/fonts/LICENSE-unifont.txt)
- Because this is a hobby project (and a fairly niche use case), I am using [Claude Code](https://www.anthropic.com/product/claude-code) to build a large part of the backend C++ code and structure the modules.  If you have concerns with that, I am glad to talk through it.  Also, please feel free to fork this repo, update any aspects and tailor things to your own use case; that's why the source is fully open and available.
- Thank you to Plex, Jellyfin, Emby and Open-Meteo for providing open and free apis to enable building modules for each.
- Thank you to [the MPV team](https://mpv.io/) for a simple, extensible and cross platform media player
- And thank you to the [Raspberry Pi Foundation](https://www.raspberrypi.org/) for helping me fill a drawer with SBCs to tinker with and inspire fun ideas like this project ❤️

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for the full text.

You are free to use, study, and modify this code. If you distribute a modified version, you must also distribute it under GPL-3.0 and make the source available.
