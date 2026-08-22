// Ghostband CLI.
//
// This exists so the musical engine can be judged by ear long before any plugin
// scaffolding exists: it turns a song plan into a MIDI file you can drop onto
// SSD5 and MODO Bass in Reaper. The same engine sources become the VST3 later,
// so nothing here belongs in the audio path - this is the test harness.

#include "ghostband/Groove.h"
#include "ghostband/Profile.h"
#include "ghostband/Render.h"
#include "ghostband/SongPlan.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string directoryOf (const std::string& path)
{
    const size_t slash = path.find_last_of ("/\\");
    return slash == std::string::npos ? std::string() : path.substr (0, slash + 1);
}

bool fileExists (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    return static_cast<bool> (f);
}

// Profiles and plans live in the source tree, but the executable is built into
// build/bin. Rather than force the user to care, look in the obvious places.
std::string resolvePath (const std::string& argv0, const std::string& path)
{
    if (fileExists (path)) return path;

    const std::string exeDir = directoryOf (argv0);
    const char* prefixes[] = { "", "../", "../../", "../../../" };

    for (const char* p : prefixes)
    {
        const std::string candidate = exeDir + p + path;
        if (fileExists (candidate)) return candidate;
    }

    return path;   // let the caller report a clean "could not open" error
}

std::string baseName (const std::string& path)
{
    const size_t slash = path.find_last_of ("/\\");
    std::string name = slash == std::string::npos ? path : path.substr (slash + 1);
    const size_t dot = name.find_last_of ('.');
    if (dot != std::string::npos) name = name.substr (0, dot);
    return name;
}

void printUsage()
{
    std::cout <<
        "Ghostband - writes a full drums-and-bass arrangement as a MIDI file.\n"
        "\n"
        "  ghostband render <plan.json> [options]\n"
        "  ghostband calibrate [options]\n"
        "\n"
        "Options:\n"
        "  -o, --out <file.mid>     output file (default: <plan name>.mid)\n"
        "      --seed <n>           override the plan's seed\n"
        "      --drums <file.json>  drum driver profile\n"
        "      --bass <file.json>   bass driver profile\n"
        "      --tuning <name>      standard | drop_d | drop_c | b_standard\n"
        "  -h, --help               this text\n"
        "\n"
        "render     builds the song described by the plan.\n"
        "calibrate  plays every mapped drum voice and bass articulation in turn,\n"
        "           with a marker naming each one, so you can check a profile\n"
        "           against the real plugin by ear.\n";
}

std::string twoDecimals (double v)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), "%.2f", v);
    return std::string (buf);
}

std::string timecode (double seconds)
{
    const int total = static_cast<int> (seconds + 0.5);
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%d:%02d", total / 60, total % 60);
    return std::string (buf);
}

void reportProfile (const std::string& label, const std::string& name,
                    bool needsVerification, const std::string& note)
{
    std::cout << "  " << label << ": " << name;
    if (needsVerification) std::cout << "   [UNVERIFIED]";
    std::cout << "\n";
    if (needsVerification && ! note.empty())
        std::cout << "      " << note << "\n";
}

} // namespace

int main (int argc, char** argv)
{
    using namespace gb;

    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    const std::string argv0 = argv[0];
    const std::string command = argv[1];

    if (command == "-h" || command == "--help" || command == "help")
    {
        printUsage();
        return 0;
    }

    if (command != "render" && command != "calibrate")
    {
        std::cerr << "ghostband: unknown command \"" << command << "\"\n\n";
        printUsage();
        return 1;
    }

    std::string planPath;
    std::string outPath;
    std::string drumProfilePath;
    std::string bassProfilePath;
    std::string tuningOverride;
    bool  haveSeedOverride = false;
    unsigned seedOverride = 0;

    int i = 2;
    if (command == "render")
    {
        if (argc < 3)
        {
            std::cerr << "ghostband render: a plan file is required\n";
            return 1;
        }
        planPath = argv[2];
        i = 3;
    }

    for (; i < argc; ++i)
    {
        const std::string a = argv[i];
        const bool hasNext = (i + 1 < argc);

        if      ((a == "-o" || a == "--out") && hasNext)  outPath = argv[++i];
        else if (a == "--drums" && hasNext)               drumProfilePath = argv[++i];
        else if (a == "--bass" && hasNext)                bassProfilePath = argv[++i];
        else if (a == "--tuning" && hasNext)              tuningOverride = argv[++i];
        else if (a == "--seed" && hasNext)
        {
            seedOverride = static_cast<unsigned> (std::atoi (argv[++i]));
            haveSeedOverride = true;
        }
        else if (a == "-h" || a == "--help") { printUsage(); return 0; }
        else
        {
            std::cerr << "ghostband: unrecognised option \"" << a << "\"\n";
            return 1;
        }
    }

    // ---- plan ------------------------------------------------------------
    SongPlan plan;
    std::string error;

    if (command == "render")
    {
        const std::string resolved = resolvePath (argv0, planPath);
        if (! SongPlan::load (resolved, plan, error))
        {
            std::cerr << "ghostband: " << error << "\n";
            return 1;
        }
    }

    if (haveSeedOverride)     plan.seed = seedOverride;
    if (! tuningOverride.empty()) plan.bassTuning = tuningOverride;
    if (! drumProfilePath.empty()) plan.drumProfile = drumProfilePath;
    if (! bassProfilePath.empty()) plan.bassProfile = bassProfilePath;

    // ---- profiles --------------------------------------------------------
    DrumProfile kit;
    BassProfile bass;

    const std::string drumPath = resolvePath (argv0, plan.drumProfile);
    if (! DrumProfile::load (drumPath, kit, error))
    {
        std::cerr << "ghostband: " << error << "\n";
        return 1;
    }

    const std::string bassPath = resolvePath (argv0, plan.bassProfile);
    if (! BassProfile::load (bassPath, bass, error))
    {
        std::cerr << "ghostband: " << error << "\n";
        return 1;
    }

    // ---- calibrate -------------------------------------------------------
    if (command == "calibrate")
    {
        if (outPath.empty()) outPath = "ghostband-calibration.mid";

        if (! writeCalibrationMidi (plan, kit, bass, outPath, error))
        {
            std::cerr << "ghostband: " << error << "\n";
            return 1;
        }

        std::cout << "Calibration file written: " << outPath << "\n\n";
        reportProfile ("drums", kit.name, kit.needsVerification, kit.verificationNote);
        reportProfile ("bass ", bass.name, bass.needsVerification, bass.verificationNote);
        std::cout << "\nLoad it in Reaper, route the drum track to your kit and the bass\n"
                     "track to MODO, and listen. Each marker names the voice or\n"
                     "articulation that should be sounding underneath it.\n";
        return 0;
    }

    // ---- render ----------------------------------------------------------
    const std::vector<std::string> warnings = plan.validate();

    const RenderResult result = renderPerformance (plan, kit, bass);

    if (outPath.empty())
        outPath = baseName (planPath) + ".mid";

    if (! writeMidi (plan, result.performance, kit, bass, outPath, error))
    {
        std::cerr << "ghostband: " << error << "\n";
        return 1;
    }

    // ---- report ----------------------------------------------------------
    std::cout << "\n" << plan.title << "\n";
    std::cout << std::string (plan.title.size(), '=') << "\n\n";

    std::cout << "  key      : " << plan.key << " " << plan.mode << "\n";
    std::cout << "  tempo    : " << twoDecimals (plan.bpm) << " bpm, "
              << plan.timeSigNumerator << "/" << plan.timeSigDenominator << "\n";
    std::cout << "  style    : " << plan.style
              << "   bass: " << plan.bassTuning << ", " << plan.playStyle << "\n";
    std::cout << "  dials    : complexity " << twoDecimals (plan.complexity)
              << ", humanize " << twoDecimals (plan.humanize)
              << ", seed " << plan.seed << "\n";
    std::cout << "  ending   : " << plan.ending << "\n\n";

    reportProfile ("drums", kit.name, kit.needsVerification, kit.verificationNote);
    reportProfile ("bass ", bass.name, bass.needsVerification, bass.verificationNote);
    std::cout << "\n";

    std::printf ("  %-12s %-10s %5s %5s %-11s %7s %6s\n",
                 "section", "role", "bar", "bars", "feel", "drums", "bass");
    std::printf ("  %s\n", std::string (64, '-').c_str());

    for (const SectionReport& s : result.sections)
    {
        std::printf ("  %-12s %-10s %5d %5d %-11s %7d %6d\n",
                     s.name.substr (0, 12).c_str(),
                     s.role.substr (0, 10).c_str(),
                     s.startBar + 1,
                     s.bars,
                     s.feel.substr (0, 11).c_str(),
                     s.drumHits,
                     s.bassNotes);
        std::printf ("  %-12s %s\n", "", s.chords.c_str());
    }

    std::cout << "\n  " << result.totalBars << " bars, " << timecode (result.durationSeconds)
              << ", " << result.performance.drums.size() << " drum hits, "
              << result.performance.bass.size() << " bass notes\n";

    if (! warnings.empty())
    {
        std::cout << "\n  warnings:\n";
        for (const std::string& w : warnings)
            std::cout << "    - " << w << "\n";
    }

    std::cout << "\nWritten: " << outPath << "\n";

    if (kit.needsVerification || bass.needsVerification)
        std::cout << "\nAt least one profile is unverified. Run \"ghostband calibrate\"\n"
                     "and check it against the real plugin before trusting the mapping.\n";

    return 0;
}
