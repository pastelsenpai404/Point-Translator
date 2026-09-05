# Architecture

Thai Karaoke Overlay is split by responsibility so Windows UI code, OCR, and
network translation can evolve independently.

```text
main.cpp
  -> app/overlay_application
       -> config/app_config
       -> services/ocr_service
       -> services/translation_service
            -> core/models + core/text
```

## Modules

| Module | Responsibility |
| --- | --- |
| `main.cpp` | Windows entry point only |
| `app/overlay_application` | Window lifecycle, tray icon, hotkeys, screen selection, and rendering |
| `app/settings_window` | Five-tab Control Center, field validation, and modal lifecycle |
| `config/` | Loading and validating runtime configuration |
| `services/ocr_service` | Windows OCR engines and automatic language candidates |
| `services/translation_service` | OpenAI-compatible HTTP request and response parsing |
| `core/` | Shared domain models and encoding/text utilities |

## Dependency rules

- `core` has no dependency on application or service modules.
- `config` and `services` may depend on `core`, never on `app`.
- `app` coordinates services and owns Win32 state.
- Provider-specific behavior stays in `translation_service`.
- Windows OCR implementation details stay in `ocr_service`.

These boundaries keep future changes—such as replacing Ollama, adding tests, or
changing the overlay UI—from requiring edits across unrelated modules.
