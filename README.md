# Thai Karaoke Overlay

A lightweight Windows C++ background app that translates selected text from any
language into:

- **คำอ่านคาราโอเกะภาษาไทย** — the original sentence's sound written with Thai letters
- **ภาษาไทย** — a natural Thai translation
- **Word-by-word guide** — Chinese segmentation, pinyin, Thai pronunciation,
  contextual meaning, and a short Thai grammar/slang explanation

It has no third-party runtime dependencies. Networking and the overlay use native
Windows APIs.

## Quick run

Right-click `Run-Thai-Karaoke-Overlay.ps1` and choose **Run with PowerShell**, or
run it from Developer PowerShell for VS 2022:

```powershell
.\Run-Thai-Karaoke-Overlay.ps1
```

The script builds the program when necessary, creates `config.ini` on first run,
restarts an existing process, and starts the overlay. To force a clean rebuild
of the executable, use `-Rebuild`.

With `-Rebuild`, the existing overlay keeps running while CMake builds into a
staging directory. Only after a successful build does the script force-stop the
old process, replace the executable, and launch the new version.

If the C++ build tools are not installed, this command installs the required
Visual Studio 2022 Build Tools workload through WinGet and then builds the app:

```powershell
.\Run-Thai-Karaoke-Overlay.ps1 -InstallBuildTools
```

The Build Tools download is relatively large and requires a Windows UAC prompt.

To enter an OpenAI API key without placing it in PowerShell command history, run:

```powershell
.\Run-Thai-Karaoke-Overlay.ps1 -ConfigureApiKey
```

The script stores it in the current user's environment and restarts the overlay.

## Local AI (recommended for privacy)

To use the lightweight `qwen3:4b-instruct` model locally instead of an online API:

```powershell
.\Run-Thai-Karaoke-Overlay.ps1 -UseLocalAI
```

The script installs Ollama when needed, downloads the approximately 2.5 GB
model, starts its local server, updates `config.ini`, and restarts the overlay.
Selected text then stays on this computer. Subsequent launches use the normal
command without `-UseLocalAI`.

## How it works

1. For selectable text, select it and press **Ctrl+Alt+T**.
2. For game or image text, press **Ctrl+Alt+O** and drag a rectangle over it.
3. The app copies selectable text or runs Windows OCR on the captured region,
   translates the result, and opens an always-on-top overlay near the bottom of
   the current monitor.
4. Use the mouse wheel when the word-by-word table has more rows.
5. Press **Esc** to hide it. Press **Ctrl+Alt+Q** to exit.

The app sits in the notification area. Double-click its icon to open Settings,
or right-click it for the full menu.

## Settings UI

Double-click the notification-area icon, or right-click it and choose
**Settings...** to open the five-tab Control Center:

- **Provider** — API URL, model, local/cloud status, and API key
- **Reading** — original text, Thai pronunciation, explanation, and word table
- **OCR** — automatic detection or a specific Windows OCR language
- **Appearance** — opacity, screen position, and automatic hiding
- **Shortcuts** — translate, screen OCR, and exit hotkeys

Changes are validated, saved, and applied immediately. The API key is stored in
the current Windows user's environment rather than in `config.ini`.

Screen capture OCR uses the lightweight OCR engine built into Windows. With the
default `ocr_language=auto`, it runs the installed Windows OCR languages and
lets the configured AI choose the most coherent result before translating. Set
a language tag such as `zh-Hans-CN` in `config.ini` only when you want to force
one OCR language.

## Build

Install **Visual Studio 2022 Build Tools** with these components:

- Desktop development with C++
- MSVC v143 C++ build tools
- Windows 10 or Windows 11 SDK
- CMake tools for Windows

Then open **Developer PowerShell for VS 2022** in this folder:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable will be at:

```text
build\Release\ThaiKaraokeOverlay.exe
```

## Source architecture

The program is organized into application, configuration, core, and service
layers. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for module ownership
and dependency rules.

## Configure

Copy `config.example.ini` to `config.ini` beside the executable. The default uses
an OpenAI-compatible chat-completions endpoint. Set your key as a per-user
environment variable so it is not saved in the project:

```powershell
setx THAI_OVERLAY_API_KEY "your-api-key"
```

Start a new terminal after running `setx`, then launch the app. For another
OpenAI-compatible provider, change `api_base` and `model` in `config.ini`. A
local provider may omit the key; the app only requires one for the default
`api.openai.com` service.

Do not commit `config.ini`; it may contain a secret if you choose to put the key
there.

## Start automatically with Windows

Press `Win+R`, enter `shell:startup`, and place a shortcut to
`ThaiKaraokeOverlay.exe` in that folder.

## Privacy

Selected text is sent to the API configured in `config.ini`. Password fields
usually cannot be copied, but do not trigger the shortcut on sensitive text.
