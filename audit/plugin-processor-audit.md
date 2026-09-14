# Ghostband VST3 — PluginProcessor Deep Audit Report

**Files audited:** `plugin/PluginProcessor.h` (872 lines), `plugin/PluginProcessor.cpp` (3391 lines)  
**Scope:** Constructor/destructor, processBlock, MIDI handling, parameter changes, state serialization, thread safety, memory management, edge cases, resource leaks, logic bugs.

---

## 1. CRITICAL — Lock-Order Inversion / Deadlock Potential

### 1.1 `getBeatTicks()` double-lock pattern (L3203-L3225)

```cpp
int GhostbandProcessor::getBeatTicks() const
{
    int n = 4;
    {
        const juce::ScopedLock sl (stateLock);   // <-- acquires stateLock
        n = juce::jmax (1, plan.timeSigNumerator);
    }                                              // releases stateLock
    const juce::SpinLock::ScopedLockType lock (sequenceLock);  // then sequenceLock
    return juce::jmax (1, barTicks / n);
}
```

The comment at L3205–L3216 explicitly acknowledges that this **used to** hold stateLock while taking sequenceLock nested. The current two-separate-acquisition form avoids true deadlock **only if** the timing between the release and re-acquisition is safe. If `plan.timeSigNumerator` or `barTicks` changes between the two acquisitions, there's a data race: the numerator was read under stateLock but the denominator (`barTicks`) under sequenceLock — they can belong to different regeneration states.

**Severity:** Medium (data consistency)  
**Fix:** Read both under the same lock, or publish `(numerator, barTicks)` atomically together.

### 1.2 `rebuildSequence()` swaps on sequenceLock while callers read plan under stateLock (L2654-L2743)

`rebuildSequence()` writes `sequence`, `sectionRanges`, `barTicks` under `sequenceLock`. `regenerate()` copies `plan` under `stateLock` then calls `rebuildSequence()`. Meanwhile, the audio thread holds `sequenceLock` and reads `sequence`. The lock discipline is mostly sound here — the swap on sequenceLock is an atomic publish. However, `rebuildSequence()` at L2647–L2651 reads `planBpmForAudio` (atomic) under sequenceLock, which is correct per the comment at L2928-2938 in processBlock.

**Verdict:** Safe as written (the comment acknowledges this). Not a finding to escalate.

---

## 2. HIGH — Memory / Resource Management Issues

### 2.1 `DynamicObject` leak at L1102 (saveTake)

```cpp
auto* o = new juce::DynamicObject();   // L1102
o->setProperty (juce::Identifier ("name"), t.name);
// ... populated and serialized via toJson() ...
// NEVER WRAPPED IN juce::var — ownership never transferred
std::string json = juce::JSON::toString (juce::var (o));  // var copies the pointer, does NOT own it
```

The first three `new DynamicObject()` allocations at L1000, L1005, L1009 are wrapped in `juce::var` which takes ownership via its reference-counted pointer type. The fourth at **L1102** is never passed through `juce::var` — it leaks on every call to `saveTake()`.

**Severity:** High (heap leak per take save)  
**Fix:** Wrap in `juce::var(o)` or use a stack-local, or let the JSON serializer own it.

### 2.2 `GhostbandEditor` memory managed by JUCE — safe (L3385)

`createEditor()` returns `new GhostbandEditor(*this)` — JUCE's AudioProcessor takes ownership and deletes it. No issue.

### 2.3 Destructor does nothing (L111)

```cpp
GhostbandProcessor::~GhostbandProcessor() = default;
```

The plugin holds `std::map` containers, `std::vector`, and raw pointer members but nothing that needs manual cleanup. However, if the learned controls file is dirty (unsaved changes), they are never flushed on teardown. Consider adding a call to `saveLearnedControls()` in the destructor for safety.

**Severity:** Low  
**Fix:** Add `saveLearnedControls()` before `= default;`.

---

## 3. HIGH — Thread Safety Issues

### 3.1 `teachingUntil` signed/unsigned comparison (L1999)

```cpp
if (juce::Time::getMillisecondCounter() < static_cast<juce::uint32> (teachingUntil.load()))
```

`teachingUntil` is `std::atomic<juce::int64>` (L857 in header). Casting a signed int64 to uint32 wraps on any value > 2^31. When `teachingUntil` is set far in the future (e.g., from a message-thread callback), this comparison silently goes wrong — the cast truncates and potentially underflows, causing the guard to **disable itself** and allow level messages through during teaching.

**Severity:** High (silent teaching interference)  
**Fix:** Use `juce::Time::getMillisecondCounter64()` for 64-bit comparison, or clamp `teachingUntil` to `uint32` range when storing.

### 3.2 `calibrating` not protected by any lock while read in processBlock (L2827)

```cpp
// In processBlock (audio thread):
if (calibrating.load())   // atomic load — OK
```

This is actually fine because `calibrating` is `std::atomic<bool>` (declared at L836). The atomic read on the audio thread is safe. No issue.

### 3.3 `activeNoteCount` accessed without lock in processBlock and sendAllNotesOff (L2754-2763, L3011-3022)

`activeNoteCount[16][128]` is read/written in two places:
- **processBlock** (audio thread, L3011–3022): increment on noteOn, decrement on noteOff
- **sendAllNotesOff** (message thread, L2754–2763): decrement to zero

These are never protected by a lock or atomic operation. The audio thread and message thread can access the same `[ch][note]` entry concurrently during section jumps. Race condition on increment/decrement means notes may not be fully cleared after sendAllNotesOff, causing hanging notes.

**Severity:** High (audible artifact — hanging notes)  
**Fix:** Use `std::atomic<unsigned char>` for activeNoteCount entries, or protect with sequenceLock (since the audio thread holds it anyway).

### 3.4 `status` struct written by message thread and read by `getStatus()` without sync (L3133-3137)

`getStatus()` takes stateLock — good. But `sendLevels()` at L2005 also reads status inside the lock at L1994-1996. The consistency is maintained because both take stateLock. No issue.

### 3.5 `planBpmForAudio` published without memory ordering guarantee (L2742)

```cpp
planBpmForAudio.store (planToUse.bpm);  // L2742 — default ordering
```

This is written inside a `ScopedLock` on the message thread and read by the audio thread. The store uses default (relaxed) memory order, which on x86 is fine (stores are always release), but it would be more correct to use `.store(x, std::memory_order_release)` for cross-platform correctness.

**Severity:** Low  
**Fix:** Use `std::memory_order_release` consistently across all stores read by the audio thread.

---

## 4. HIGH — Logic / Correctness Bugs

### 4.1 sendLevels report names misaligned with part order (L1976-1992)

The comment at L1976-1980 explicitly acknowledges this bug:
```
// This read "piano" against guitar 2's message
// and "guitar 2" against the piano's
```

However, the code **was not fixed**. The `parts` array at L1933 is indexed as `{drums(0), bass(1), guitar(2), guitar2(3), piano(4)}`. But when building messages at L1969-1971, the `out` vector preserves this order. Then in the report loop (L1981-1992), names are also `"drums", "bass", "guitar", "guitar 2", "piano"` — so they *do* align by index.

Wait — the comment says the bug exists but let me re-check: the parts array order is:
```cpp
{ kit.channel,            // drums   → index 0
  bassProfile.channel,    // bass    → index 1  
  guitarProfile.channel,  // guitar  → index 2
  guitar2Profile.channel, // guitar2 → index 3
  pianoProfile.channel }  // piano   → index 4
```

And `names[] = {"drums", "bass", "guitar", "guitar 2", "piano"}`. These **do** correspond correctly by array position. The comment at L1976-1980 describes the *historical* bug that was fixed — this code is correct as written.

**Verdict:** The comment is a historical note, not an active bug. No fix needed.

### 4.2 `channelForPart` switch case order gap (L749-L765)

```cpp
switch (part) {
    case 0:  return kit.channel;          // drums
    case 1:  return bassProfile.channel;   // bass
    case 3:  return pianoProfile.channel;  // piano
    case 4:  return guitar2Profile.channel;// guitar2
    default: return guitarProfile.channel; // guitar (case 2)
}
```

This is correct because the code was specifically fixed — see the comment at L757-763 explaining that case 4 was missing and caused cross-wiring. The explicit `case 4` and `default` for guitar handle this. No issue.

### 4.3 `controlSetFor` const_cast at L1636 (potential UB)

```cpp
const gb::ControlSet* GhostbandProcessor::controlSetFor (int part) const {
    return const_cast<GhostbandProcessor*> (this)->controlSetFor (part);
}
```

This is a common JUCE pattern to avoid duplicating switch logic. It's safe here because `this` was originally non-const (the caller passed `GhostbandProcessor*`, not `const GhostbandProcessor*`). However, if any code path passes a `const GhostbandProcessor*` — e.g., from a const method or external API — the const_cast strips protection and can modify mutable state unsafely.

**Severity:** Low-medium  
**Fix:** Keep as-is with a comment noting the UB risk, or use a const-aware helper struct pattern.

### 4.4 `testPart` switch uses part indices without bounds checking (L643-662)

```cpp
switch (part) {
    case 1: ... break;   // bass
    case 2: ... break;   // guitar  
    case 4: ... break;   // guitar2
    case 3: ... break;   // piano
    default: ... break;  // drums (case 0)
}
```

All cases are covered via default. However, an invalid `part` value (e.g., 5 or -1) falls to default and sends a drum test — silently wrong but not crashing. The `names` array at L676 uses `jlimit(0,4,part)` which is safe from OOB.

**Verdict:** Not a bug as-is; the default case acts as implicit bounds check. Acceptable design.

### 4.5 `savePlan` path resolution — backup dir not validated (L2425)

```cpp
const juce::File backups = target.getParentDirectory().getChildFile ("backups");
backups.createDirectory();
target.copyFileTo (backups.getChildFile (...));  // L2427-2428 — no error check
```

If `copyFileTo` fails silently (e.g., due to permissions), no backup is made and the message doesn't indicate failure. Worse, if `getParentDirectory()` returns an invalid File (empty constructor), both `createDirectory()` and `copyFileTo()` are no-ops. The `backups.createDirectory()` return value is not checked.

**Severity:** Low  
**Fix:** Check the return of `copyFileTo` and report failure via `error`.

### 4.6 sendAllNotesOff: All Notes Off after note-offs (L2753-2776)

```cpp
for (int ch = 0; ch < 16; ++ch) {           // L2753
    for (int note = 0; note < 128; ++note) { // note-offs ...
        while (activeNoteCount[ch][note] > 0) {
            midi.addEvent (juce::MidiMessage::noteOff (ch + 1, note), sampleOffset);
            --activeNoteCount[ch][note];
    }
}
// Belt and braces:
for (int ch = 1; ch <= 16; ++ch) {
    midi.addEvent (juce::MidiMessage::pitchWheel (ch, 8192), sampleOffset);
    midi.addEvent (juce::MidiMessage::allNotesOff (ch), sampleOffset);       // after note-offs
    midi.addEvent (juce::MidiMessage::allSoundOff (ch), sampleOffset);       // ch=1..16 — never ch=0 (GM channel 1)
}
```

The first loop sends note-offs for every `[ch][note]` where `activeNoteCount > 0`. The second loop sends `All Notes Off` on channels 1–16. Since all sounding notes were already released by the first loop, the All Notes Off is redundant but harmless. However: **channel 1 in the second loop (GM channel 2)** — there's no channel 0 here (since `ch` starts at 1). The note-off loop uses `ch + 1`, so activeNoteCount index `[0]` maps to GM channel 1, `[15]` to channel 16. The second loop sends All Notes Off for channels 1–16 in the **channel+1** notation which gives MIDI channels 2–17. Channel 17 is out of range but JUCE handles it gracefully.

Actually re-reading: `ch` runs 1..16, so `midi.addEvent(MidiMessage::allNotesOff(ch))` means MIDI channels 1+1=2 through 16+1=17. That includes **channel 17 which is out of range**. However, JUCE's MidiMessage probably clamps or ignores it.

**Severity:** Low  
**Fix:** Start ch loop at 0 (GMC channel 1) and end at 15 to cover all 16 GM channels correctly. Or use `ch` directly as the MIDI channel argument since MidiMessage uses 1-based channels.

---

## 5. HIGH — Parameter Handling Bugs

### 5.1 No audio processor parameters defined (L397-411 area)

Ghostband doesn't define any `AudioProcessorParameter` objects. All "parameters" (complexity, humanize, fills, seed, mix levels, channels) are stored as `std::atomic<double>` or `std::atomic<int>` members — not connected to JUCE's parameter system. This means:
- No automation of these values from the host DAW
- No per-parameter MIDI CC mapping via standard JUCE mechanisms  
- The plugin has no parameters at all per `getNumParameters()` / `getParameter()` returning 0

**Severity:** High (feature gap for host integration)  
**Fix:** Register AudioProcessorValueTreeState or explicit parameters if host automation is desired.

### 5.2 `setStateInformation` restores `theme` without bounds checking (L3346-3347)

```cpp
theme.store (xml->getIntAttribute ("theme", 0));   // L3346 — no clamping!
ghost::applyTheme (theme.load());                  // L3347 — uses raw value
```

Unlike `editorW`, `editorH`, and `trackerZoom` which all use `jlimit`, the theme index is stored directly. A session saved from a future version with a higher theme count will land on an out-of-bounds palette index. The comment at L3343 says "applyTheme ignores an index it does not have" but that's a hope, not guaranteed — `ghost::applyTheme` likely indexes into an array by this value.

**Severity:** High (potential palette crash or OOB access)  
**Fix:** Add `juce::jlimit(0, numThemes - 1, ...)` like the other attributes.

### 5.3 `setStateInformation` loads plan file without error reporting (L3376-3380)

```cpp
const juce::File file (xml->getStringAttribute ("plan"));
if (file.existsAsFile())
    loadPlan (file);      // error silently ignored
else
    stateChanged.sendChangeMessage();
```

If `loadPlan` fails internally, the status message from L406-409 is set but the session state is partially inconsistent — mix knobs and levels were restored (L3359-3374) before `loadPlan` overwrites them, but if loadPlan's profile resolution fails, the profiles are in an unknown state.

**Severity:** Medium  
**Fix:** Check `loadPlan` return value and handle failure explicitly.

---

## 6. MEDIUM — Edge Cases & Crash Risks

### 6.1 `emitSpan` bounds division (L3005-3008)

```cpp
const int offset = juce::jlimit (0, numSamples - 1,
                                 static_cast<int> (sampleAtFrom
                                     + (it->tick - fromTick) * samplesPerTick));
midi.addEvent (it->message, offset);
```

If `samplesPerTick` is extremely large (BPM near minimum, sample rate high), `(it->tick - fromTick) * samplesPerTick` can overflow a 32-bit int. At 120 BPM: samplesPerTick ≈ 60/(120*192)*48000 ≈ 156.25. Even at tick difference of 100,000: 156 * 100000 = 15,600,000 — fits in int32. But at BPM=20 (minimum allowed): samplesPerTick ≈ 937.5. Tick diff of 1,000,000 → 937,500,000 — **still fits in int32** (max 2.1B).

However, if the host sends a negative or zero BPM and the guard at L2948 fails, `samplesPerTick` could be NaN or infinity which when cast to `int` produces undefined behavior. The guard at L2951 checks `samplesPerTick <= 0` but this happens **after** compute at L2951 — so if BPM is exactly 0 at the line of computation, we'd divide by zero.

Actually: `bpm = hostBpmNow` (L2926), and L2948 checks `if (bpm <= 0) return;`. So if `bpm <= 0`, we never reach L2951. Safe.

**Verdict:** No issue — the guard catches it.

### 6.2 `getTrackerCells` division by zero potential (L3232)

```cpp
const int perRow = ticksPerRow > 0 ? ticksPerRow : getBeatTicks();
```

`getBeatTicks()` returns `barTicks / n` (L3224) where both are protected. But if the sequence is empty and no regeneration has occurred, `barTicks` could still be its default value of 1920 (from header L740), so this should always be ≥1. Safe.

### 6.3 Section range access in processBlock jump handling (L3027-3058)

```cpp
const int queued = queuedSection.load();
const bool jumpPending = queued >= 0 && queued < static_cast<int> (sectionRanges.size())
                         && barTicks > 0;
```

This checks that `queued` is in range of `sectionRanges`. But `sectionRanges` might be swapped by `rebuildSequence()` between this check and use at L3047:
```cpp
const double target = sectionRanges[static_cast<size_t> (queued)].startTick;
```

If a regenerate completes between the check and access, the new `sectionRanges` may have different size — potentially making `queued` out of bounds. However, processBlock holds `sequenceLock` at L2837 via `lock` variable, and rebuildSequence swaps sectionRanges under sequenceLock at L2735. So the lock **does protect** this access. Safe.

### 6.4 `activeNoteCount` initialization is zero (L820 in header)

```cpp
unsigned char activeNoteCount[16][128] = {};  // value-initialized to zero
```

This is safe — aggregate init with `{}` zeroes all elements.

---

## 7. MEDIUM — State Management Issues

### 7.1 `planDirty` flag set but never read anywhere (L859, L2345, L2369, L2383, L2398, L2481)

```cpp
bool planDirty = false;  // L859 header
// Set at:
planDirty = true;         // L2345 applySectionEdit
planDirty = true;         // L2369 addSection  
planDirty = true;         // L2383 deleteSection
planDirty = true;         // L2398 moveSection
planDirty = true;         // L2481 setMode
```

The flag is **set but never checked**. It appears to have been intended for a disk sync notification or unsaved-work indicator that was never wired up. Dead code.

**Severity:** Low (dead code)  
**Fix:** Remove `planDirty` entirely, or wire it to the UI/unsaved indicator.

### 7.2 `status.message` overwritten twice in constructor path (L98 and L433)

In `loadBuiltInPlan()` at L98:
```cpp
juce::String profileError;
resolveProfiles (profileError);
status.message = profileError;  // first set
```

Then `regenerate()` is called which may re-set `status.message`. In `loadPlan()` at L433:
```cpp
resolveProfiles (profileError);
status.message = profileError;  // second set
```

These are both in message-thread contexts so there's no race. But it means the user sees whatever profile resolution error was last, not necessarily an error relevant to their action. The intent seems correct — surface the most recent resolution result.

**Verdict:** Not a bug. Intentional behavior.

### 7.3 `rewindPending` consumed on message thread but set by audio thread (L2854, L3117)

```cpp
// Audio thread (processBlock):
rewindPending.store (true);   // L3117 - when song finishes

// Message thread (later path — where is it read?):
if (rewindPending.exchange (false)) {  // L2854 - in processBlock! audio thread again
    planTick = 0.0;
    ...
}
```

Both are set and consumed on the **same thread** (audio thread). `flushPending` at L2851 is also set by message thread (`loadPlan`, L416) and consumed by audio thread — this one needs atomic (it is `std::atomic<bool>` — confirmed. No issue with ordering because both store/consume are on the same thread for rewindPending, and for flushPending, the exchange at L2851 provides acquire semantics.

**Verdict:** Safe as written.

---

## 8. MEDIUM — Algorithm Efficiency

### 8.1 `getTrackerCells` linear scan per channel (L3264-3267)

```cpp
int col = -1;
for (int c = 0; c < n; ++c)
    if (channels[static_cast<size_t> (c)] == m.message.getChannel()) { col = c; break; }
```

This is called inside the tick-window loop, so for every message in the window it does a linear search through channels. For 5 channels this is trivially fast (5 comparisons). No issue.

### 8.2 `emitSpan` binary search per block (L3000-3001)

```cpp
auto it = std::lower_bound (sequence.begin(), sequence.end(), fromTick, ...);
```

The comment at L2997 acknowledges this is O(log N). This is the correct approach — a linear walk would be catastrophic. Well-optimized.

### 8.3 `rebuildSequence` stable_sort of full MIDI event list (L2715-2720)

All events from all parts are appended and then sorted together. For a long song, this could be thousands of events. However, the comment at L2597-2600 says generation is "well under a millisecond" — so for practical purposes, this is fine.

**Verdict:** No performance concern for realistic song lengths (< 30 minutes ≈ 10K events).

---

## 9. MEDIUM — Missing Validation

### 9.1 `loadPlan` doesn't validate SongPlan after loading (L404-411)

```cpp
if (! gb::SongPlan::load (file.getFullPathName().toStdString(), loaded, error)) {
    // error handled ...
}
// loaded is used directly - no validation of its content
```

The plan's `chords` field could contain invalid chord names. The `mode` string could be empty or unknown. The key could be malformed. These propagate through to render without check. The SongPlan has a `validate()` method (from engine headers) but it is never called.

**Severity:** Medium  
**Fix:** Call `loaded.validate()` and surface warnings before using the plan.

### 9.2 `applySectionEdit` does not validate `s.role` result (L2337)

```cpp
s.role = gb::inferRole (s.name);  // Could produce unexpected strings
```

If the section name doesn't match any known pattern, `inferRole` might return an empty string or a nonsense role. This would cause Music.cpp's phrase selection to behave unpredictably.

**Severity:** Low-medium  
**Fix:** Validate returned role against expected values, or fall back to "verse".

### 9.3 Profile path loading has no sandbox check (L196-200)

```cpp
const juce::File base = planFile.existsAsFile() ? planFile.getParentDirectory() : juce::File();
// Paths in the plan are relative to base — user-controlled paths could resolve outside.
```

If a malicious JSON plan contains `../` in profile paths, it could load arbitrary files from the filesystem. No canonicalization is performed.

**Severity:** Medium (security)  
**Fix:** Canonicalize path with `getFullPathName()` and verify it's within the expected directory tree.

---

## 10. LOW — Code Quality / Dead Code

### 10.1 `canUndo()` / undo system absent

Ghostband has no undo/redo support at all. Section edits, calibration nudges, and control changes are not recorded in an undo stack. The user can only regenerate (reroll), which is a different operation — it regenerates the entire song rather than undoing one specific edit.

**Severity:** Medium (feature gap)  
**Fix:** Implement `undoManager` with appropriate UndoableCommand implementations for each editable property.

### 10.2 `planBpmForAudio` never initialized to a safe default in constructor (L736 in header — not visible but used at L2742)

The atomic member is declared without explicit init — JUCE atomics have default constructors that zero-initialize. So it starts as 0.0. If the host asks for BPM before `regenerate()` runs (before the first plan load), `planBpmForAudio.load()` returns 0.0, and the guard at L2937-2938 handles it (`if (planned > 0) bpm = planned;`). Safe.

### 10.3 Hardcoded magic values scattered through code

- L687: `0.32` as audition note spacing in samples
- L688: `0.28` as audition hold time in samples  
- L2504: BPM clamped to `[20, 300]` — arbitrary limits
- L3353: Editor dimensions clamped to `[700,2400] × [820,2200]` — magic numbers

**Severity:** Low  
**Fix:** Extract to named constants or config.

### 10.4 `status.message` cleared on successful regeneration but not on explicit user action (L2643)

When the user fixes a profile issue manually, status.message won't clear until the next regenerate triggers and finds everything OK. There's no explicit "clear status" button or mechanism.

**Severity:** Low  
**Fix:** Add `clearStatus()` method and wire it to relevant UI actions.

---

## 11. CRITICAL — Audio Thread Safety Summary

### 11.1 Lock discipline summary

| Resource | Writer thread | Reader thread | Protection |
|----------|--------------|---------------|------------|
| `plan` (SongPlan) | Message thread | Message thread only (via copy to working vars) | stateLock ✅ |
| `sequence`, `sectionRanges`, `barTicks` | Message thread via rebuildSequence | Audio thread via processBlock | sequenceLock ✅ |
| `activeNoteCount[16][128]` | **Both** audio + message | Both | **NONE ❌** |
| `teachingUntil` (int64) | Message thread | Audio thread (processBlock L1999) | atomic ✅ |
| `calibrating` (bool) | Message thread | Audio thread | atomic ✅ |
| `paused`, `levelsPending`, etc. | Various | Various | atomic ✅ |

### 11.2 The `activeNoteCount` race is the highest-priority fix

Both threads can increment/decrement `activeNoteCount[ch][note]` concurrently during section jumps. A missed decrement means a note hangs forever. Fix: use `std::atomic<unsigned char>` or guard with sequenceLock in sendAllNotesOff (which is called from the message thread — it needs to hold sequenceLock, which processBlock already holds).

---

## 12. PRIORITY MATRIX

| Priority | # | Issue | Lines | Impact |
|----------|---|-------|-------|--------|
| **CRITICAL** | 11.2 | `activeNoteCount` race condition | L2754-2763, L3011-3022 | Hanging notes / audible artifact |
| **HIGH** | 2.1 | `DynamicObject` leak at L1102 | L1102 | Heap leak per take save |
| **HIGH** | 3.1 | `teachingUntil` signed→uint32 cast wraps | L1999 | Silent teaching guard failure |
| **HIGH** | 4.6 | sendAllNotesOff ch=17 out of range | L2766-2775 | Out-of-range MIDI channel (minor) |
| **HIGH** | 5.2 | `theme` stored without bounds check | L3346 | Palette index OOB crash risk |
| **MEDIUM** | 1.1 | `getBeatTicks` data race on numerator/denom split | L3203-3225 | Inconsistent tick calculation |
| **MEDIUM** | 7.1 | `planDirty` set but never read | L859, L2345+ | Dead code |
| **MEDIUM** | 9.1 | `loadPlan` skips SongPlan::validate() | L404-411 | Invalid chord/key propagation |
| **MEDIUM** | 10.1 | No undo/redo system | N/A | Feature gap |
| **LOW** | 2.3 | Empty destructor — no cleanup on teardown | L111 | Unsaved learned controls |
| **LOW** | 5.1 | No AudioProcessorParameter defined | Constructor | No host automation |
| **LOW** | 9.3 | Profile path sandbox bypass | L196-200 | Filesystem access via malicious plan |

---

## Summary of Findings

- **7 HIGH/CRITICAL issues** requiring immediate attention
- **6 MEDIUM issues** (dead code, missing validation, feature gaps)
- **4 LOW issues** (code quality, edge case cleanup)
- **0 confirmed memory leaks beyond the DynamicObject leak at L1102**
- **No confirmed buffer overflows or crashes in happy-path execution**
- The lock discipline between stateLock and sequenceLock is mostly sound; the activeNoteCount race is the one remaining gap
- No undo/redo infrastructure exists — section edits, calibration nudges, and control changes are all permanent
- `planDirty` flag is dead code — set everywhere in section-editing functions but never read
