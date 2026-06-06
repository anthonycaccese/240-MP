# Contributing to 240-MP

Thank you for considering contributing to 240-MP!  I originally built this as personal project for watching video content on a CRT but I'm also stoked to see what ideas you might want to add.  So with that in mind I'll try to make contributing to this project as easy and transparent as possible.  If you have any questions on the below please create a post in [Discussions > Q&A](https://github.com/anthonycaccese/240-MP/discussions/categories/q-a).

## Non-code contributions

The most useful community contributions are often not code, items like the following are super helpful...

- Documentation improvements where a step was unclear
- Hardware validation reports for different Pi models and CRT outputs
- Logs from failures along with step by step ways to replciate
- Photos or screenshots of working setups

## Submitting code

### Principles to keep in mind

1. **Baseline on remote control as an input device**: All experiences should be built so they can be interacted with via up/down/left/right enter and esc/backspace.  More complex inputs should be avoided so that users can navigate via a simple usb remote.
2. **Lay out screens for 240p/480i on a CRT**: Design layouts and size elements to display well on a CRT TV.  Consider overscan when placing elements on screen.  If you leverage the `root.sh` and `root.sw` vars for sizing you'll get responsive display for LCD tvs out of the box.
3. **Keep modules self contained**:  If your module just relies on QML then you can simply add your module in a /modules/[module name] directory with a manifest.json and 240-MP will pick it up for display.  If your module requires a backend then you'll also need to register it in /src/main.cpp.  But other than that please keep all of your module source in a /src/modules/[module name] folder.
4. **No tracking or analytics**: Do not include any mechanisms for tracking or reporting usage to an external source that you maintain.  A module should only ever write details to the local 240-MP configuration directory.  If a module relies on connecting to a 3rd party API (example: the Plex module) then it should only communicate with that API directly.
5. **Browse & Hand-off**: Think of 240-MP and its modules as a way to browse structured content (either on a filesystem or via an API response) and to hand-off to a purpose built tool for an action (like how it relies on MPV for video playback which is purpose built for that ask).  The approach is to leverage existing, purpose built applications that exist on a system and not bundle everything into 240-MP.

### Note on AI Use

- I used Claude Code as part of my work building 240-MP so the use of AI tools for development is very much allowed. With that in mind, contributors are expected to own and understand the code they submit and any communication in a PR (including code, code comments, and GitHub comments) must come from a human contributor, not an AI agent acting autonomously.
- Pull requests should include details description that outline the scope of AI involvement (e.g. which parts were AI-generated and what human testing or review was performed prior to submission). PRs that omit this disclosure may be closed without review.

### Use a Consistent Coding Style

- Please follow the same style as the source you are editing.
- If you are contributing new code, keep the style consistent with other similar works.
- Parameterize as much as possible, try to avoid hard coded values whenever you can.

### Set up your environment

Please review [BUILDING.md](BUILDING.md) for details on how to set up an environment

## License

By contributing, you agree your contributions are licensed under GPL (see [LICENSE](LICENSE)).
