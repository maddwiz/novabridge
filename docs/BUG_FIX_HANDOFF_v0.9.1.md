# BUG FIX HANDOFF — NovaBridge v0.9.1

## Priority

- Priority: Low
- Ship-blocking: no

## Bug

Infinite recursion in `NovaBridgeSetPlaybackTime` on UE `< 5.7`.

## File + Location

- File: `NovaBridge/Source/NovaBridge/Private/NovaBridgeModule.cpp`
- Function: `NovaBridgeSetPlaybackTime`
- Current lines: around `113-131` (at time of writing)

## What is wrong

In the pre-5.7 branch, the scrub path recursively calls `NovaBridgeSetPlaybackTime(...)` with the same arguments, causing infinite recursion and eventual stack overflow.

Current broken block:

```cpp
if (bScrub)
{
    NovaBridgeSetPlaybackTime(Player, TimeSeconds, true);
}
```

## Recommended fix

Replace the recursive scrub call with a direct UE playback call on pre-5.7 builds.

Safer cross-version fallback:

```cpp
static void NovaBridgeSetPlaybackTime(ULevelSequencePlayer* Player, float TimeSeconds, bool bScrub)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
    FMovieSceneSequencePlaybackParams Params;
    Params.PositionType = EMovieScenePositionType::Time;
    Params.Time = TimeSeconds;
    Params.UpdateMethod = bScrub ? EUpdatePositionMethod::Scrub : EUpdatePositionMethod::Jump;
    Player->SetPlaybackPosition(Params);
#else
    Player->JumpToSeconds(TimeSeconds);
#endif
}
```

Notes:
- `ScrubToSeconds(...)` may exist in some UE minors; `JumpToSeconds(...)` is safer and acceptable for NovaBridge's editor-time agent workflows.

## How to trigger

1. Build NovaBridge against UE `< 5.7`.
2. Call `POST /nova/sequencer/scrub` with payload like:
   - `{"sequence":"...","time":2.0}`
3. Handler path: `/nova/sequencer/scrub` → `NovaBridgeSetPlaybackTime(Player, Time, true)`.
4. Pre-5.7 branch recurses until crash.

## Verification plan after fix

1. Build against UE `< 5.7` (or temporarily force the pre-5.7 branch).
2. `POST /nova/sequencer/create {"name":"Test","path":"/Game"}`
3. `POST /nova/sequencer/scrub {"sequence":"/Game/Test","time":1.0}`
4. Expect `{"status":"ok"}` and no crash.

## Risk assessment

Low risk. This branch only compiles on UE `< 5.7`. Current validated environments are on UE 5.7+.

