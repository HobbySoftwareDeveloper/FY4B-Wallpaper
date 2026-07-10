# FY4B-Wallpaper

A lightweight Windows background utility that automatically fetches the latest earth full-disk images from the FengYun-4B (FY-4B) satellite every 10 minutes, processes them, and sets them as your desktop wallpaper.

## Features
* **Smart Cropping**: Automatically detects your current screen resolution and aspect ratio, cropping the image from the center to prevent stretching or distortion.
* **Low Overhead**: Built with GDI+ using bilinear interpolation to minimize CPU and memory usage on low-end PCs.
* **Network Interruption Defense**: Implements strict binary validation for JPEG `0xFF 0xD8` (SOI) and `0xFF 0xD9` (EOI) markers. It automatically blocks corrupted or partial downloads caused by network instability and retries safely.
* **Local Backup Fallback**: Automatically rolls back to the last successful local wallpaper cache if network requests continuously fail.
* **Auto-Cleanup**: Automatically deletes older timestamped history files to save your disk space.

## Requirements
* Windows 10 / 11
* Visual Studio 2022 (with C++20 support)

## How to Run
1. Clone or download this repository.
2. Double-click `FY4B-Wallpaper.slnx` to open the project in Visual Studio.
3. Build and run the project under **Release** configuration.

## License
MIT License
