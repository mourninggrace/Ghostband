# Shipping Ghostband commercially

The intent to sell Ghostband was stated on 2026-08-23. This is the list of things
that have to be true before money changes hands, ordered by how expensive they
get if left late.

**None of this is legal advice, and licence terms change.** Every licensing claim
below should be re-read from the source before relying on it.

## Licensing

### JUCE — the one that needs a decision

JUCE is dual-licensed: **AGPLv3 or a commercial licence**. AGPL is not an option
for a closed-source product — it would require releasing the source under AGPL to
anyone who receives the binary.

As of late 2025 the free **Starter** tier permits commercial closed-source use up
to **$20,000 annual revenue**, and JUCE 8 no longer requires a splash screen on
the free tier. Above that threshold a paid tier is required.

Practical consequences:

- Register for a JUCE licence before the first sale, even at $0.
- `JUCE_DISPLAY_SPLASH_SCREEN=0` is already set in `CMakeLists.txt`. That is
  permitted on JUCE 8's free tier, but *was not* on JUCE 6/7 — do not carry this
  assumption backwards if the JUCE version is ever pinned lower.
- Revenue thresholds are per year and are checked on the honour system, but the
  licence is a contract. Track it.

Sources to re-read: <https://juce.com/get-juce/>, <https://juce.com/legal/juce-8-licence/>

### VST3 SDK — resolved

Steinberg relicensed the VST3 SDK to **MIT** in late 2025. There is no longer a
proprietary agreement to sign and no GPL obligation. Nothing to do.

(ASIO is separate and went GPLv3/proprietary dual — irrelevant here, since
Ghostband never touches an audio driver directly.)

### The engine itself

`engine/` has no third-party code at all — no JSON library, no PRNG library,
nothing. That was worth the effort: the entire licensing surface is JUCE, and
only the plugin layer touches it. The CLI links only the engine, so it could be
distributed under any terms at all.

## Blocking, and cheap to fix now

- **JUCE is not vendored.** `CMakeLists.txt` falls back to
  `C:/Projects/Polygraph/external/JUCE`. The build therefore depends on an
  unrelated project existing on this machine, and will fail on any other. Add it
  as a pinned git submodule before anyone else ever builds this.
- **Profiles ship as loose files.** The built-in plan works with no files at all,
  but any external plan needs its profiles resolvable. Decide where a customer's
  profiles live and how the plugin finds them.

## Required before sale

- **Code signing.** An unsigned plugin from an unknown publisher trips Windows
  SmartScreen and reads as untrustworthy. A standard code-signing certificate is
  a recurring annual cost and materially affects how the product is perceived.
- **A real installer.** `Install.bat` is a development convenience, not a
  product. It also currently refuses to run while Gig Performer is open, which is
  correct behaviour and worth keeping in whatever replaces it.
- **Copy protection, or a considered decision not to have any.** There is none
  today. Options run from honour-system, through a simple offline key, to an
  activation service. This is a real subsystem, not a checkbox.
- **Verified driver profiles.** Shipping profiles marked `[UNVERIFIED]` to paying
  customers is a support burden. The in-plugin **Calibrate** feature moves from a
  nice-to-have to a product requirement — it is what lets a customer make
  Ghostband work with instruments nobody has profiled.

## Decisions that affect the business model

- **Who pays for the AI planner?** If it calls the Claude API, either the
  customer supplies their own key (no cost to you, more friction for them) or you
  bundle it (smooth, but a per-generation cost against a one-off purchase price).
  This shapes pricing, so decide it before building the planner, not after.
- **Trademarks.** Shipping profiles named for SSD5, MODO Bass 2 and UJAM titles
  is ordinary nominative use — "works with X" — but marketing must not imply
  endorsement or partnership by those vendors.
- **Scope of the promise.** "Works with any plugin" is the aspiration and it is
  reachable, but a customer who buys expecting it and finds an unprofiled plugin
  needs Calibrate to exist and to be easy. Do not market ahead of that.

## Rough market comparables

Not a recommendation, and pricing is a judgement call that belongs to the person
selling it. For orientation only, this category sits roughly where these do:

| product | kind | approx price |
| --- | --- | --- |
| Scaler 2 | chord/progression assistant | ~$60 |
| Captain Plugins Epic | songwriting suite | ~$79 |
| UJAM Virtual Guitarist (each) | phrase instrument | ~$99–149 |
| EZkeys / EZbass | phrase instrument + songwriting | ~$100–180 |

Ghostband is closer in ambition to the last two than the first, and it is the
only one that drives *instruments the customer already owns* rather than shipping
its own sounds. That is the distinctive claim and it is worth pricing against.
