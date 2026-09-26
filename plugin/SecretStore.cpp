#include "SecretStore.h"

#if defined (_WIN32)

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wincrypt.h>

#pragma comment (lib, "crypt32.lib")

namespace gbsecret
{
    // Any fixed bytes will do; they are mixed into the protection so that a
    // blob from some other program using DPAPI for the same user cannot be
    // handed to this one and decrypted as though it were Ghostband's.
    static const char kEntropy[] = "Ghostband planner key v1";
    static unsigned long gLastError = 0;

    unsigned long lastError() { return gLastError; }

    // THE ENTROPY MUST OUTLIVE THE CALL. Both functions used to build it as
    //     blobOf (std::string (kEntropy, ...))
    // - a temporary destroyed at the end of that line, leaving the blob
    // pointing at freed memory. CryptProtectData/CryptUnprotectData then read
    // whatever the heap had put there since. Usually the old bytes were still
    // intact, so it worked; sometimes they were not, and a perfectly good key
    // was refused with ERROR_INVALID_DATA (13). The owner re-entered his key
    // twice on 2026-09-26 before the logged error code pointed here. A static
    // lives for the whole program, so the pointer is always to the real bytes.
    static const std::string& entropyBytes()
    {
        static const std::string e (kEntropy, sizeof (kEntropy) - 1);
        return e;
    }

    static DATA_BLOB blobOf (const std::string& s)
    {
        DATA_BLOB b;
        b.pbData = reinterpret_cast<BYTE*> (const_cast<char*> (s.data()));
        b.cbData = static_cast<DWORD> (s.size());
        return b;
    }

    bool protect (const std::string& plain, std::string& cipher)
    {
        DATA_BLOB in      = blobOf (plain);
        DATA_BLOB entropy = blobOf (entropyBytes());
        DATA_BLOB out {};

        // UI_FORBIDDEN: a plugin must never pop a system dialog in the middle
        // of somebody's gig.
        if (! CryptProtectData (&in, L"Ghostband", &entropy, nullptr, nullptr,
                                CRYPTPROTECT_UI_FORBIDDEN, &out))
            return false;

        cipher.assign (reinterpret_cast<const char*> (out.pbData), out.cbData);
        LocalFree (out.pbData);
        return true;
    }

    bool unprotect (const std::string& cipher, std::string& plain)
    {
        DATA_BLOB in      = blobOf (cipher);
        DATA_BLOB entropy = blobOf (entropyBytes());
        DATA_BLOB out {};

        if (! CryptUnprotectData (&in, nullptr, &entropy, nullptr, nullptr,
                                  CRYPTPROTECT_UI_FORBIDDEN, &out))
        {
            gLastError = GetLastError();
            return false;
        }
        gLastError = 0;

        plain.assign (reinterpret_cast<const char*> (out.pbData), out.cbData);

        // Wipe before freeing. The key does not need to outlive this call in
        // any buffer this code does not own.
        SecureZeroMemory (out.pbData, out.cbData);
        LocalFree (out.pbData);
        return true;
    }
}

#else

// Not Windows: no store, so no key, so no planner - and nothing else changes.
// Never a dependency.
namespace gbsecret
{
    bool protect   (const std::string&, std::string&) { return false; }
    bool unprotect (const std::string&, std::string&) { return false; }
    unsigned long lastError() { return 0; }
}

#endif
