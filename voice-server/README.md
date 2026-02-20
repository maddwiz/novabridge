# NovaBridge Voice Server

Sidecar service for speech-to-text and natural-language command execution against NovaBridge.

## Install

```bash
python -m venv .venv
. .venv/bin/activate
pip install -r voice-server/requirements.txt
```

## Run

```bash
python voice-server/voice_server.py
```

Default port: `30012`

## Environment

- `NOVABRIDGE_HOST` (default: `localhost`)
- `NOVABRIDGE_PORT` (default: `30010`)
- `NOVABRIDGE_API_KEY` (optional)
- `NOVABRIDGE_VOICE_PORT` (default: `30012`)
- `NOVABRIDGE_WHISPER_MODE` (`local` or `api`)
- `NOVABRIDGE_WHISPER_MODEL` (`tiny`, `base`, `small`, `medium`, `large`)
- `OPENAI_API_KEY` (required when `NOVABRIDGE_WHISPER_MODE=api`)

## Endpoints

- `GET /voice/health`
- `POST /voice/transcribe`
  - multipart: `audio=@file.wav`
  - or json: `{"audio_path":"..."}`
- `POST /voice/command`
  - json: `{"text":"spawn 3 point lights"}`
  - or audio input

