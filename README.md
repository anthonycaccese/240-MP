<img 
src="https://github.com/user-attachments/assets/73c3e46f-a74a-4d96-9c4f-ae30f28378be" 
/>

# 240-MP — Monterey Intel Edition

This repository is a fork of the original **240-MP** project by Anthony 
Caccese.

Its primary goal is to provide a stable, native Intel (x86_64) build for 
**macOS Monterey**, while keeping the fork as close as possible to the 
upstream project. Whenever possible, improvements developed here are 
contributed back to the original repository through Pull Requests.

## What's different in this fork

- Native Intel (x86_64) target for macOS Monterey.
- Portable macOS application bundle.
- Fixes the thin white border around fullscreen mpv playback by removing the 
`--no-native-fs` launch argument.
- Tracks upstream development while keeping Monterey Intel compatibility.
- Fullscreen fix submitted upstream as Pull Request #140.

## Philosophy

This fork intentionally keeps its changes to a minimum.

Features and improvements that are useful to all platforms are proposed to 
the upstream project whenever possible. Changes that are specific to Intel 
Macs or macOS Monterey remain in this fork.

## Upstream Project

The original 240-MP project is available at:

https://github.com/anthonycaccese/240-MP

---

The remainder of this README is the original upstream documentation.
