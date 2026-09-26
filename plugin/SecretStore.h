#pragma once

#include <string>

// Encryption for the one secret Ghostband holds: the owner's own API key for the
// AI planner.
//
// Windows' Data Protection API, scoped to the current user. The bytes on disk
// can only be turned back into the key by the same Windows account on the same
// machine - copying the file to another machine, or reading it as another user,
// yields nothing. That is the right strength for this: it defeats the file
// being picked up by a backup, a sync folder or a support zip, which is how
// keys actually leak, without asking the owner to remember a password.
//
// Deliberately free of JUCE. It needs <windows.h>, which brings macros that
// collide with half of JUCE's names, so it lives in a file that includes
// nothing else and speaks only std::string.
namespace gbsecret
{
    // True on success. `cipher` is opaque bytes, not text.
    bool protect   (const std::string& plain,  std::string& cipher);
    bool unprotect (const std::string& cipher, std::string& plain);

    // Windows' own error code from the last unprotect that failed, 0 if none.
    // Logged, so a key Windows refuses to decrypt says why instead of leaving
    // it to guesswork - which is all there was on 2026-09-26.
    unsigned long lastError();
}
