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

## Local Knowledge Base (RAG)

TitanAI can index your own documents and ground its answers in them — fully offline,
using **Ollama embeddings** and a local vector index. Nothing is uploaded.

- **Index a folder or files** from the sidebar **Knowledge Base** button (books icon) or
  the Developer Hub card. Supported inputs include PDF, Markdown, plain text, notes and
  most source-code/config formats (recursive folder indexing skips `.git`, `node_modules`,
  `build`, caches, etc.).
- **Automatic grounding** — once documents are indexed, normal chat answers are retrieved
  with cosine similarity over your chunks and answered with source citations. Toggle this
  off in the Knowledge Base dialog if you prefer plain chat.
- **Explicit questions** — say *"ask my documents about …"*, *"search my notes for …"*,
  *"according to my documents …"* or *"what do my notes say about …"*.
- **Manage the index** — say *"index my documents"*, *"index /path/to/folder"*,
  *"knowledge base status"*, *"remove <path> from my knowledge base"*, or
  *"clear knowledge base"*. The dialog also supports add/remove/reindex/clear and shows
  live progress.
- **Citations** — grounded replies mention the source file (and line range) used.

Embedding setup (one-time):

```bash
# Small, fast embedding model (~270 MB)
ollama pull nomic-embed-text

# PDF text extraction (optional, for .pdf files)
sudo pacman -S poppler
```

TitanAI uses Ollama's `/api/embeddings` endpoint (falling back to `/api/embed` on newer
servers). To use a different embedding model, set the environment variable before launch:

```bash
TITANAI_EMBED_MODEL=mxbai-embed-large ./build/TitanAI
```

The index is stored locally under your application data directory
(`~/.local/share/TitanAI/TitanAI/rag/index.json` by default) and rebuilds automatically
if you switch embedding models.

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

## Disk Cleanup

TitanAI can monitor your disk usage and suggest safe cleanup actions, tuned for Arch Linux:

- Click **Analyze Disk Usage** in the Developer Hub (or ask in the chat):
  - *"analyze my disk usage for cleanup"*
  - *"free up disk space"*
  - *"clean up my cache"*
- **Disk usage monitoring** lists every real mounted file system with total/used/free
  space and a fill percentage, and warns when a volume is nearly full.
- **Cleanup suggestions** measure common space hogs and propose safe commands:
  - Pacman package cache (`sudo paccache -rk1`)
  - User application cache (`~/.cache`)
  - Trash (`~/.local/share/Trash`)
  - Systemd core dumps (`/var/lib/systemd/coredump`)
  - Journal logs (`journalctl --disk-usage`, then `journalctl --vacuum-size`)
  - Orphan packages (`pacman -Qtdq`, then `pacman -Rns`)

Directory sizes are measured with a batched background scan so the UI stays responsive.
Suggestions are previews only — TitanAI never deletes files on its own; run the suggested
commands yourself after reviewing them.

## Update Checker

TitanAI tracks your installed package versions and tells you what can be updated:

- Click **Check for Updates** in the Developer Hub (or ask in the chat):
  - *"check my system for updates"*
  - *"are there any pending updates"*
  - *"upgrade my packages"*
- **Installed package tracking** reads every installed package and its version with
  `pacman -Q` and reports the total count.
- **Update detection** compares installed versions against the repositories:
  - Official repos via `checkupdates` (pacman-contrib) — safe, unprivileged, never
    touches the real database. Falls back to `pacman -Qu` (last synced state) if it is
    not installed.
  - AUR packages via `paru -Qua` or `yay -Qua`, when a helper is available.
- The report lists every outdated package (`old -> new`), grouped official-first, and
  suggests the commands to apply them (`sudo pacman -Syu`, `paru -Syu`).

TitanAI never installs anything on its own — review the list first, then update yourself.

## Export Conversations

Save any chat log as **Markdown** or **PDF**:

- **From the main chat window** — click the **⬇ Export** button in the chat header
  (or press **Ctrl+H** to open the history), pick *Markdown (.md)* or *PDF (.pdf)*,
  choose where to save, and the current conversation is exported as a styled,
  human-readable transcript.
- **From the history panel** — select any saved conversation and use the
  **⬇ Export** menu (Markdown / PDF / Plain Text) in the bottom-left action bar.

Exports include message timestamps, sender labels, and image-attached markers.
MD files are plain Markdown; PDFs are paginated A4 documents with a clean
print-oriented theme generated locally — nothing is uploaded anywhere.

## Meeting Notes (Record & Summarize)

Capture a meeting with the microphone, transcribe it live, take typed notes,
then generate a local AI summary and export the whole thing:

- **Record & transcribe** — click the **Record Meeting** sidebar button (or press
  **Ctrl+M**). Speech is captured continuously and transcribed utterance-by-utterance
  with Vosk; speak for as long as you like, then press **Stop Meeting**.
- **Manual notes** — type notes anytime in the "Add note" field, even without a
  microphone; they are merged into the transcript with a 📝 marker.
- **Generate Summary** — the accumulated transcript is summarized by the local
  Ollama model (the current chat model) into suggested title, key points,
  decisions, action items, and open questions.
- **Send Summary to Chat** — drop the summary directly into the active chat and
  chat history.
- **Export** — save the meeting as **Markdown** (`.md`) or **PDF** (`.pdf`),
  including the summary block and every timestamped entry. Everything runs
  locally.

> **Note:** Live transcription requires Vosk to be installed and rebuilt
> (see [Voice Features](#voice-features)). Without it, Meeting Notes still works
> as a manual note-taker and summarizer.

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


## AI Terminal Assistant

TitanAI includes a secure, local-first **AI Terminal** that turns natural-language
requests into reviewed Linux commands. It uses the same local Ollama model as the
chat assistant and runs entirely on your machine.

Open **AI Terminal** from the sidebar (terminal icon), or just ask in chat, for example:

- *"Find the 5 largest files in my Downloads folder."*
- *"Check how much RAM my computer is using."*
- *"Find all PDF files in my Documents folder."*
- *"Show available disk space."*
- *"Explain this command: `df -h`"*

### How it works

1. **Describe the task** in the AI Terminal input and click **Generate Command**.
2. TitanAI asks the local model for a structured JSON proposal (executable + arguments,
   explanation, risk level, privilege requirement, working directory).
3. The proposal is parsed as JSON and passed through `CommandSafetyValidator`, a
   deterministic layer that does **not** trust the model's risk label.
4. TitanAI displays the command, explanation, risk badge, working directory and any
   warnings.
5. **Nothing runs automatically.** Click **Execute** and confirm the approval dialog.
6. Output, errors, exit code and duration appear live. **Stop Execution** terminates a
   running process.
7. Click **Explain Output** to have the local model interpret the (sanitized) output.
8. Every operation is recorded in local history with copy/regenerate actions.

The same flow is available from chat: terminal requests are detected, a preview is shown,
and the AI Terminal page opens for approval. Chat never executes a command on its own.

### Security model

The initial release is deliberately conservative:

- **Executable allowlist** — only a small set of read-only and low-impact programs
  (`free`, `df`, `du`, `ls`, `ps`, `find`, `stat`, `mkdir`, `touch`, `cp`, `mv`, `ln`, …)
  can execute. Read-only `git` subcommands (`status`, `log`, `diff`, `branch`, `show`,
  `rev-parse`) are also allowed, while global options, destructive branch operations and
  protected filenames are blocked. Unknown programs are blocked.
- **No shell** — commands use `QProcess` with a program and literal argument list. `sh -c`,
  `bash -c`, pipes, redirection, command substitution, `;`, `&&`, globs passed to a shell
  and other compound syntax are rejected.
- **No privilege escalation** — `sudo`, `su`, `pkexec`, `doas`, package managers and other
  system-modifying tools are blocked, even if the model labels them safe.
- **High-risk commands blocked** — deletion, permission changes, system configuration,
  interpreters, downloaded scripts and similar operations are blocked by default. You can
  still copy the command and review/run it manually.
- **Path boundaries** — commands may only access your home directory and temporary
  directories. Protected paths such as `/etc/shadow`, `/root`, `~/.ssh`, key material and
  credential-like filenames are rejected. Symlinks are resolved, and dereference flags
  (`-L`, `-H`, `--dereference`, `find -follow`) are blocked so a link inside your home
  directory cannot be used to reach protected files.
- **Output limits and sanitization** — output sent for explanation is length-limited and
  obvious passwords/tokens/private keys are redacted. Output is explicitly wrapped as
  untrusted data so the model does not follow instructions embedded inside it.
- **No auto-execution** — generation, preview and explanation never start a process.

Because a safe single command cannot express every workflow (for example a real shell
pipeline for sorting the largest files), TitanAI prefers limited, reviewable functionality
over unrestricted shell access. Use **Copy Command** if you need to run something manually.

### Local history

Command metadata (original request, command, explanation, risk, timestamp, status, exit
code, duration) is stored locally under the application data directory
(`~/.local/share/TitanAI/TitanAI/terminal/history.json` by default). Terminal output is
**not** stored. Use the **Clear** button in the AI Terminal history panel to delete it.

### Tests

The automated suite covers proposal parsing, invalid model JSON, allowlisting, risk
classification, injection/substitution attempts, privilege escalation, path boundaries,
approval enforcement, cancellation, timeouts, output capture, history persistence and
untrusted-output sanitization:

```bash
./build/titanai_tests
```

## Desktop Automation Agent

TitanAI includes a local-first **Desktop Automation Agent**. Describe a desktop task in
natural language and TitanAI builds a structured plan, validates it, and waits for your
explicit approval before touching any application or window.

Open **Desktop Automation** from the sidebar (window-layout icon), or ask in chat:

- *"Open VS Code on the left and the browser on the right."*
- *"Arrange the windows side by side."*
- *"Move the terminal to the right side of the screen."*
- *"What windows are currently open?"*
- *"Focus the browser window."*
- *"Open my project folder."*
- *"Show git status in my project."*

### How it works

1. **Describe the task** and click **Generate Plan**.
2. The local Ollama model returns a strict JSON plan. The model output is untrusted.
3. TitanAI parses the plan, checks installed applications and desktop capabilities, and
   marks unavailable steps as **Unsupported** with a clear reason.
4. `AutomationPolicy` validates every action: allowed action types only, safe parameters,
   authorized paths, duplicate/action limits, and read-only terminal commands.
5. The plan, permissions, warnings and per-step status are shown in the page. The plan is
   fingerprinted with SHA-256.
6. **Nothing runs automatically.** Click **Approve & Execute** and confirm. Execution is
   bound to the fingerprint you approved and the plan is re-validated immediately before
   running.
7. Steps run asynchronously in order. A required failure stops the workflow and marks the
   remaining steps **Skipped**. **Stop Automation** cancels a running workflow.
8. **Retry Failed Step** requires a fresh confirmation. History entries can be reopened
   for review and a new approval, but never re-execute without one.

### Supported desktops

- **Hyprland (Wayland)** — full window management through `hyprctl`: list, focus, move,
  resize, active window, screen geometry, and wait-for-window. Modern Hyprland releases
  (0.56+, Lua dispatchers) are supported, with a fallback to the legacy dispatcher syntax
  for older versions.
- **X11** — window management through `wmctrl` when it is installed (`xprop` is used for
  the active window). If `wmctrl` is missing, window actions are reported as unsupported.
- **Other compositors** — application launch, directory/file open and terminal actions
  still work; window actions fail gracefully with an explanation. No unsafe fallback is
  attempted.

Application launching uses only system-trusted `.desktop` directories and never invokes a
shell. Shell/interpreter entries and entries containing shell syntax are rejected.

### Security model

- **Explicit approval only** — generation never launches, moves or executes anything.
  Approval is bound to the immutable plan fingerprint.
- **Allowlisted action types** — launch, focus, list, move, resize, open directory/file,
  get active window, wait for window and approved terminal commands.
- **No arbitrary shell** — application launches use tokenized `.desktop` `Exec` entries
  with `QProcess`; terminal steps reuse the AI Terminal's shell-free executor.
- **Low-risk terminal only** — automation terminal steps must pass `CommandSafetyValidator`
  at low risk. `sudo`, destructive commands, shell operators and protected paths are
  blocked.
- **Path boundaries** — file actions are limited to the project directory, home directory
  and `/tmp`; traversal, symlink escapes and protected paths are rejected.
- **Limits** — at most 12 actions per plan and at most 2 identical action signatures, to
  prevent runaway loops.
- **No invented window ids** — the planner is instructed to use application names;
  window ids are resolved locally at execution time.

### Local history

Automation metadata (request, task, plan, per-step status and short messages) is stored at
`~/.local/share/TitanAI/TitanAI/automation/history.json` with owner-only permissions.
Raw terminal output is deliberately **not** persisted. History is capped at 100 entries and
can be cleared from the page.

### Tests

The suite includes 18 desktop-automation tests covering JSON plan parsing, malformed and
unknown actions, intent detection, path/traversal/protected-path policy, restricted git
commands, approval and fingerprint enforcement, mocked launch/focus/move execution,
unsupported-step skipping, required-failure stopping, wait timeout, cancellation, history
sanitization/persistence, `.desktop` safety and graceful unsupported window backends:

```bash
./build/titanai_tests
```

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
