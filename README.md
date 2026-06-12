<img src="https://github.com/user-attachments/assets/73c3e46f-a74a-4d96-9c4f-ae30f28378be" />

# 240-MP

240-MP is a retro VCR style frontend to play content on Raspberry Pi (preferably hooked up to a CRT TV). 

Playback experiences are handled via modules to enable new integrations without requiring major changes to the overall frontend. There are 3 currently included playback modules; one for [Local Files](https://github.com/anthonycaccese/240-MP/wiki/Module:-Local-Files), one for [Plex](https://github.com/anthonycaccese/240-MP/wiki/Module:-Plex) and a module similar to art/wallpaper modes on modern tvs called ([Ambient:Mode](https://github.com/anthonycaccese/240-MP/wiki/Module:-Ambient-Mode))

It's built to work in conjuction with MPV which will be installed (or updated) as a dependency during the [install](#Install) steps outlined below.

## Video Overview

Watch on YouTube: https://youtu.be/r-gylGDoELY

## Photos

| Module Selection | Item Detail |
| --- | --- |
| <img src="https://github.com/user-attachments/assets/9472d55a-4617-4a7f-80c4-32aa28494048" /> | <img src="https://github.com/user-attachments/assets/4f7d8230-860a-4ace-9370-9f59f43289c0" /> |

| Resume Option | Playback | Settings |
| --- | --- | --- | 
| <img src="https://github.com/user-attachments/assets/490e9ebd-fab2-4fd1-9959-35ebb619eff0" /> | <img src="https://github.com/user-attachments/assets/a3c768c7-6ede-4cdf-9d03-90aee7b8cdfb" /> | <img src="https://github.com/user-attachments/assets/0fd48977-8776-4334-b34e-d12256f23b97" /> |

## Current Features

### Local Files Module ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Local-Files))
- Supported file types: `"mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "flv", "f4v", "mpg", "mpeg", "vob"`
- Playlist support using `m3u` and `m3u8` files
- Folder browsing
- Loop playback support
- Playback history options
- Switch audio/subtitle tracks during playback

### Plex Module ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Plex))
- Designed for CRT navigation (simple, fast, list browsing)
- Supported library types: `Movies, TV Shows, Other Videos`
- Server switching
- User profile switching and auto sign in
- Select specific libraries to display
- Continue Watching and Resume
- Hub, Playlist, Collection and Category support
- Movie editions
- Select preferred audio/subtitle track before playback and switch tracks during playback
- Full library browsing by letter
- Show/Season browsing
- Video quality selection: Direct Playback (Default) or Transcode options

### Ambient:Mode Module ([Wiki](https://github.com/anthonycaccese/240-MP/wiki/Module:-Ambient-Mode))
- Supported video file types: `"mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "flv", "f4v", "mpg", "mpeg", "vob"`
- Playlist support for audio tracks using `m3u` and `m3u8` files
- Mix video with a different audio track
- Loops forever until you stop it

### Global
- [Color Schemes](https://github.com/anthonycaccese/240-MP/wiki/Customizations)
- [Keyboard & Controller](https://github.com/anthonycaccese/240-MP/wiki/Input) input support

## Install 

- [On a Raspberry Pi](INSTALL.md#on-a-raspberry-pi)
- [On macOS (ARM)](INSTALL.md#on-macos-arm)

## FAQs

- Why didn't you use Kodi/LibreELEC/OSMC?
    - I've used all of those distros and they are all excellent but I also like making things and wanted something simpler without as many options.  Something that felt like a VCR from my youth.
- Should I use 240-MP instead of Kodi/LibreELEC/OSMC?
    - Nope 😄
    - All of those distros are amazing, feature rich, work across a ton of devices and have awesome supportive teams behind them.
    - I on the other hand am just one person making nostalgic things for my own niche use cases.  If those use cases match with what you're looking for, then 240-MP is a bunch of fun and I'd be happy for you to try it.  Otherwise, the well known distros are spectacular and you should likely open those doors instead.
- Will this work on other Raspberry Pi models? (like the 5, 2 zero, etc...)
    - Sorry, I can't say for sure as I've only tested on the 4b, 3b+ and 3b and don't have plans to test on other devices at this time.
- Where does the name "240-MP" come from?
    - 240 has a double meaning referring to the longest [VHS tape length](https://en.wikipedia.org/wiki/VHS#Tape_lengths) and my primary display target for it of [CRT TVs](https://consolemods.org/wiki/CRT:What_is_240p%3F).
    - MP also has a double meaning of "Media Player" and a play on the "SP/LP/EP/SLP" terminology that was used to refer to the recording quality for VHS recordings.
- Does the 240 in the name mean that it outputs at 240p resolution?
    - No and I apologize for any confusion I've caused on this, it's 240 in name only.
    - The output resolution for the menu and video playback when using it on a CRT is 480i.
- Does 240-MP work over HDMI on a modern television too?
    - Yes! The UI was built to scale on modern televisions over HDMI as well.
    - Please make sure you use the config.txt I provide for HDMI and it will output at the proper resolution for a modern tv.

## Credits & Acknowledgments 

- The `VCR OSD Mono` font was created by Riciery Santos Leal (a.k.a. mrmanet) https://www.dafont.com/vcr-osd-mono.font
- Because this is a hobby project (and a fairly niche use case), I am using [Claude Code](https://www.anthropic.com/product/claude-code) to build a large part of the backend C++ code and structure the modules.  If you have concerns with that, I am glad to talk through it.  Also, please feel free to fork this repo, update any aspects and tailor things to your own use case; that's why the source is fully open and available.
- Thank you to Plex for providing an open and free [API](https://developer.plex.tv/) with all the endpoints needed for me to make my own custom client
- Thank you to [the MPV team](https://mpv.io/) for a simple, extensible and cross platform media player
- And thank you to the [Raspberry Pi Foundation](https://www.raspberrypi.org/) for helping me fill a drawer with SBCs to tinker with and inspire fun ideas like this project ❤️

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for the full text.

You are free to use, study, and modify this code. If you distribute a modified version, you must also distribute it under GPL-3.0 and make the source available.
