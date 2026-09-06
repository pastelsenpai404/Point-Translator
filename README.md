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
staging directory. Only after a successful build does the new executable request
a normal close of the old application, verify it has exited, replace the executable,
and launch the new version. It matches the exact executable path and never force-kills
the process. If shutdown does not finish within 15 seconds, the update stops and the
old executable remains intact. Finish any translation or close Settings before retrying.

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

The app sits in the notification area. Double-click its icon to open Point Translator,
or right-click it for the full menu.

## Point Translator workspace

Choose the source and target above the input: **จีน (zh)**, **อังกฤษ (en)**,
or **ไทย (th)**. The **⇄** button swaps them. Selections persist and also apply to
clipboard hotkeys and screen OCR. OCR requires the corresponding Windows OCR language.
The main translation and copy button use the selected target; pronunciation and
learning explanations stay in Thai. Suggested replies use the selected target language.
History stores both languages, the target translation, and the engine; older Thai history remains readable.

Choose **AI** to use the configured model for everything, or **Argos + AI** for an
Argos translation with AI-generated reading aids and replies. If AI is unavailable,
the Argos translation is still displayed. Chinese ↔ English and English ↔ Thai use
direct models; Chinese ↔ Thai uses English as an intermediate language.

To install Argos and its four language packages once:

```powershell
.\Start-Argos.ps1 -Install -Python 'C:\path\to\python.exe'
```

Python 3.10+ is required. Dependencies are isolated in `.argos-venv`; initial setup
downloads packages and models. The application starts the installed loopback bridge
automatically on subsequent launches. `Start-Argos.ps1` can also start it manually.
The bridge binds only to `127.0.0.1:18765`. Argos runs locally; AI reading aids and
replies use your configured local/cloud provider. The bridge remains available after
closing the UI and does not log translation text. Integration uses the published
[Argos Translate library](https://github.com/argosopentech/argos-translate).

The app now opens a resizable workspace with Thai labels. Type or paste text and
choose **แปลข้อความ**, or use **จับภาพ OCR** for screen text. The three result tabs
show the full translation and pronunciation, suggested replies, and word explanations.
Long results can be scrolled and selected for copying.

- **คำตอบแนะนำ 3 แบบ** asks the configured model for a polite/professional reply,
  a friendly/casual reply, and a clarifying question. Each includes the target-language
  reply and its Thai meaning. **คัดลอกคำตอบ** copies only the target-language reply;
  the app never sends a reply automatically. Missing suggestions are explicitly shown.
- The history sidebar searches original, target and Thai text. Selecting an entry restores
  its translation, words and replies without another API request.
- The latest 500 successful translations are saved in
  `%LOCALAPPDATA%\PointTranslator\history.json`, including local timestamps. This is
  an unencrypted local JSON file. Use **ลบรายการ** or **ล้างประวัติ** to remove entries.
  Read/write failures are displayed; unreadable history is preserved instead of overwritten.
- The compact overlay and existing hotkeys remain available. Choose **History / Replies**
  on the overlay, or double-click the tray icon, to return to the workspace. Closing the
  workspace keeps the app in the tray; use the configured quit hotkey or tray **Exit** to quit.
- Only one app instance runs per Windows session to avoid conflicting history writes.

## Settings UI

Click **ตั้งค่า** in the workspace, or right-click the notification-area icon and choose
**Settings...** to open the Thai Settings window with five sidebar sections:

- **Provider** — API URL, model, local/cloud status, and API key
- **Reading** — original text, Thai pronunciation, explanation, and word table
- **OCR** — automatic detection or a specific Windows OCR language
- **Appearance** — opacity, screen position, and automatic hiding
- **Shortcuts** — translate, screen OCR, and exit hotkeys

Settings includes Ollama/OpenAI presets, a show/hide API-key button, an editable
OCR-language dropdown, Thai position labels, and reset buttons for appearance and
shortcuts. Switching provider presets clears the draft key when the service changes.
The footer indicates edits; Cancel discards them. Invalid fields reveal their section.
The window scales its controls and fonts to the monitor DPI and available work area.

Changes are validated, saved, and applied immediately. The API key is stored in
the current Windows user's environment rather than in `config.ini`.

Screen capture OCR uses the lightweight OCR engine built into Windows. With the
default `ocr_language=auto`, it selects the Windows OCR language matching the source
language in the workspace. That OCR language must be installed in Windows. Set a
language tag such as `zh-Hans-CN` in `config.ini` to override this selection explicitly.

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

### Service integration checks

Configure with `-DPOINT_TRANSLATOR_TESTS=ON` and build Release. Run
`node tests/mock_provider.cjs` in one terminal, then run
`build\Release\PointTranslatorServiceTests.exe <new-absolute-test-json-path> http://127.0.0.1:18764`.
Use a new path inside the build directory, never your real history file. The checks cover
three replies, Thai/Chinese/emoji and escaped text persistence, clearing, corrupt-file
preservation, HTTP errors, and providers that omit suggestions. They use a local fixture
and do not require an API key or call an external model.

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
