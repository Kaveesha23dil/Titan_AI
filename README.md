# TitanAI

TitanAI is a lightweight, local-first AI assistant specifically designed for **Arch Linux** users.

## Project Vision
TitanAI aims to deliver a privacy-respecting, high-performance desktop AI companion integrated tightly into the Arch Linux system environment. It is engineered from the ground up to be fast, efficient, and zero-bloat while providing system automation and local AI capabilities.

## Current Project Status
- **Milestone 1**: Initial project foundation setup (C++20, CMake, Ninja).
- **Status**: Pre-alpha foundation. Advanced features (LLM engine, agent architecture, database, GUI, tools integration) will be introduced in subsequent milestones.

## Technology Stack
- **Language**: C++20
- **Compiler**: GCC
- **Build System**: CMake (>= 3.20)
- **Generator**: Ninja
- **Target OS**: Arch Linux
- **IDE**: Antigravity IDE
- **GUI**: Qt 6 (Core, Network, Widgets)
- **Voice (optional)**: Qt 6 Multimedia (microphone capture), Qt 6 TextToSpeech / `espeak-ng` (spoken replies), Vosk (offline speech-to-text)

## Image Analysis

TitanAI can analyze images captured from your camera or selected from disk using a
vision-capable local model (the default model `gemma3:4b` supports images):

- Say **"open camera"** in the chat (or click the **Camera** button) to open the live
  camera dialog, point it at an object, and press **Capture**.
- Or click **Image** to pick an image file instead.
- The captured/selected image is attached to your next message. Type a question (e.g.
  *"suggest me what things I can create using this board"*) and press **Send** — the
  assistant analyzes the image and answers.

The image is downscaled and compressed before being sent to the model, and a thumbnail
of every analyzed image is shown in the chat.

## File Organization

TitanAI can help you tidy up messy folders. It works fully offline on any directory:

- Click **Organize && Find Duplicates** (uses the Project directory, or your home folder
  if none is set), or just ask in the chat:
  - *"find duplicate files in /home/you/Downloads"*
  - *"suggest a folder structure for my files"*
- **Duplicate detection** compares file contents with SHA-256 (files are first grouped by
  size so only real candidates are hashed) and reports every group of identical files
  together with the wasted disk space.
- **Folder suggestions** categorize your files by type (Documents, Images, Videos, Audio,
  Archives, Code, Data, Fonts, Misc) and propose a clean folder layout.

Suggestions are previews only — TitanAI never moves or deletes files on its own.

## Voice Features

TitanAI includes a fully local voice assistant built on top of the chat GUI:

- **Voice input (speech-to-text)** — press the **Mic** button, speak, and the transcript is
  inserted into the chat. Auto-stop on silence (energy-based VAD) or a hard 45 s cap.
- **Wake word** — enable hands-free listening (default wake word "hey titan"). When the wake
  word is heard, the assistant starts listening automatically.
- **Auto-send** — send the message as soon as you stop speaking.
- **Spoken replies (text-to-speech)** — assistant responses are read aloud with selectable
  voice, rate, pitch, and volume. Listening is paused while speaking so the assistant never
  talks over you.
- **Live mic level meter** and voice status indicator in the chat window.
- All voice settings persist between sessions (`Voice Settings` button).

Voice input uses **Vosk**, a lightweight offline recognizer. Install it (AUR) and rebuild to
enable it:

```bash
paru -S vosk-api
```

Download a model and set its path in **Voice Settings** (e.g. the small English model):

```bash
wget https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip
unzip vosk-model-small-en-us-0.15.zip
```

Spoken replies work out of the box with `qt6-speech` (speech-dispatcher) or `espeak-ng`.
Without Vosk the app still builds and runs; only voice input is disabled.


## Building the Project

### Prerequisites
Ensure you have CMake, Ninja, and GCC installed on Arch Linux:
```bash
sudo pacman -S cmake ninja gcc
```

### Build Instructions
Configure the build system using CMake and Ninja:
```bash
cmake -S . -B build -G Ninja
```

Build the application target:
```bash
cmake --build build
```

## Running the Project
After building, execute the binary:
```bash
./build/TitanAI
```

## License
This project is licensed under the [MIT License](LICENSE).
