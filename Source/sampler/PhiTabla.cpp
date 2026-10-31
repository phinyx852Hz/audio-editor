// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiTabla.h"

#include <cctype>
#include <map>

namespace phitabla
{
    const std::vector<Pad>& pads()
    {
        // Bank A: the dāyāṅ and the compounds; bank B: the bāyāṅ, the rest, the phrases and Sa.
        static const std::vector<Pad> all {
            { 0, "dha", "dha", { "ta-1", "ge-1" } },
            { 1, "dha 2", "dha", { "ta-2", "ge-2" } },
            { 2, "dhin", "dhin", { "tin-1", "ge-1" } },
            { 3, "dhin 2", "dhin", { "tin-2", "ge-2" } },
            { 4, "dhi", "dhi", { "te-1", "ge-1" } },
            { 5, "dhi 2", "dhi", { "te-3", "ge-2" } },
            { 6, "dhun", "dhun", { "tu-1", "ge-1" } },
            { 7, "kran", "kran", { "ke-1", "ta-1" } },
            { 8, "tā", "ta", { "ta-1" } },
            { 9, "tā 2", "ta", { "ta-2" } },
            { 10, "tin", "tin", { "tin-1" } },
            { 11, "tin 2 (sur)", "tin", { "tin-2" } },
            { 12, "te", "te", { "te-1" } },
            { 13, "te 2", "te", { "te-2" } },
            { 14, "te 3", "te", { "te-3" } },
            { 15, "tu", "tu", { "tu-1" } },
            { 16, "tu 2", "tu", { "tu-2" } },
            { 17, "ge", "ge", { "ge-1" } },
            { 18, "ge 2", "ge", { "ge-2" } },
            { 19, "ge (mīṇḍ)", "", { "ge-meend" } },
            { 20, "ke (kath)", "ke", { "ke-1" } },
            { 21, "ke 2 (kath)", "ke", { "ke-2" } },
            { 22, "tak", "tak", { "tak" } },
            { 23, "trika", "trika", { "trika" } },
            { 24, "phrase: dhātete (bharī)", "", { "phrase-dhatete-bhari" } },
            { 25, "phrase: tātete (khālī)", "", { "phrase-tatete-khali" } },
            { 26, "phrase: tirakiṭe roll", "", { "phrase-tirakite-roll" } },
            { 27, "phrase: gege ×3", "", { "phrase-gege-x3" } },
            { 28, "phrase: ge modulations", "", { "phrase-gege-modulations" } },
            { 29, "Sa: dāyāṅ (C♯)", "", { "sa-dayan-c-sharp" } },
            { 30, "Sa: bāyāṅ (F♯)", "", { "sa-bayan-f-sharp" } },
        };
        return all;
    }

    std::string bolOf (const std::string& syllable)
    {
        std::string s;
        for (char c : syllable)
            s += (char) std::tolower ((unsigned char) c);
        while (! s.empty() && (s.back() == '\'' || std::isdigit ((unsigned char) s.back())))
            s.pop_back();
        static const std::map<std::string, std::string> as {
            { "dha", "dha" }, { "dhaa", "dha" }, { "dhin", "dhin" }, { "dhi", "dhi" }, { "dhee", "dhi" }, { "dhe", "dhi" }, { "dhet", "dhi" },
            { "dhun", "dhun" }, { "kran", "kran" }, { "kr", "kran" },
            { "ta", "ta" }, { "taa", "ta" }, { "na", "ta" }, { "naa", "ta" }, { "ne", "ta" },
            { "tin", "tin" }, { "ti", "te" }, { "tee", "te" }, { "te", "te" }, { "tet", "te" }, { "ra", "te" }, { "re", "te" },
            { "tu", "tu" }, { "tun", "tu" }, { "thun", "tu" },
            { "ge", "ge" }, { "ghe", "ge" }, { "ga", "ge" }, { "gaa", "ge" }, { "gi", "ge" },
            { "ke", "ke" }, { "ka", "ke" }, { "ki", "ke" }, { "kat", "ke" }, { "kath", "ke" },
            { "tak", "tak" }, { "trika", "trika" },
        };
        const auto f = as.find (s);
        return f == as.end() ? std::string() : f->second;
    }

    int padFor (const std::string& syllable, int use)
    {
        const auto bol = bolOf (syllable);
        if (bol.empty())
            return -1;
        std::vector<int> takes;
        for (const auto& p : pads())
            if (bol == p.bol)
                takes.push_back (p.pad);
        if (takes.empty())
            return -1;
        return takes[(size_t) ((use % (int) takes.size() + (int) takes.size()) % (int) takes.size())];
    }

    int noteFor (const std::string& syllable, int use)
    {
        const auto pad = padFor (syllable, use);
        return pad < 0 ? -1 : slicer::noteForPad (pad, slicer::Layout::push, 0);
    }

    slicer::Kit kit (const std::string& folder, const std::function<sampler::SamplePtr (const std::string&)>& load, std::vector<std::string>* missing)
    {
        slicer::Kit k;
        k.layout = slicer::Layout::push;
        k.velocity = 0.8;
        k.velocityTone = 0.5;
        k.voices = 32;
        std::map<std::string, sampler::SamplePtr> loaded;
        for (const auto& p : pads())
        {
            int layer = 0;
            for (const auto* f : p.files)
            {
                const auto path = folder + "/" + f + ".wav";
                auto& s = loaded[f];
                if (s == nullptr)
                {
                    s = load (path);
                    if (s == nullptr && missing != nullptr)
                        missing->push_back (path);
                }
                if (s != nullptr && layer < slicer::maxLayers)
                    slicer::placeSound (k, p.pad, layer++, s, path);
            }
            auto& pad = k.pads[(size_t) p.pad];
            pad.name = p.name;
            pad.colour = *p.bol == 0 ? 9 : p.pad < 16 ? 3 : 6;
            // Strokes ring out; the open ones ring a while, so a stroke stops the last one on its drum.
            pad.release = 0.08;
            const std::string b = p.bol;
            pad.choke = b == "ge" || b == "ke" ? 2 : (b == "ta" || b == "tin" || b == "te" || b == "tu" || b == "tak" || b == "trika") ? 1 : 0;
        }
        return k;
    }
}
