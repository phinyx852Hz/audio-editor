// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiTabla.h"

#include <cctype>
#include <algorithm>
#include <map>

namespace phitabla
{
    const std::vector<Pad>& pads()
    {
        // Bank A: the dāyāṅ and the compounds; bank B: the bāyāṅ, the rest, the phrases and Sa.
        // Banks A and B: the owner's Ableton Drum Rack ("TablaDrumRackV.2"), notes 61 to 92 in order,
        // with its start and end points and levels; bank C: compounds layered, and Sa.
        static const std::vector<Pad> all {
            { 0, "ta-titrakitetakatirakite", "", { { "phrase-ta-titrakitetaka-tirakite", 0, 56282, 8.0 } } },
            { 1, "ta-titrakitetaka", "", { { "phrase-ta-titrakitetaka", 0, 74585, 8.0 } } },
            { 2, "dhātetedhātetetāgedhināgenā", "", { { "phrase-dhatete-bhari", 0, 95999, 5.0 } } },
            { 3, "tātetetātetetāketinākenā", "", { { "phrase-tatete-khali", 0, 121618, 7.0 } } },
            { 4, "tirakite-roll4", "", { { "phrase-tirakite-roll", 0, 95999, 20.0 } } },
            { 5, "dhātidhāgetinākenā", "", { { "phrase-dhatidhagetinakena", 0, 130396, 8.0 } } },
            { 6, "tātitāketinākenā", "", { { "phrase-tatitaketinakena", 0, 97544, 4.0 } } },
            { 7, "dhāgenā", "", { { "phrase-dhagena", 0, 57145, 8.0 } } },
            { 8, "trika", "trika", { { "trika", 362, 17804, 6.0 } } },
            { 9, "tete2", "", { { "phrase-tatete-2", 0, 26753, 9.0 } } },
            { 10, "tete1", "", { { "phrase-tatete-2", 0, 26753, 9.0 } } },
            { 11, "tātete", "", { { "phrase-tatete-1", 0, 41699, 8.0 } } },
            { 12, "gegegegex2", "", { { "phrase-gege-x2", 0, 94353, 0.0 } } },
            { 13, "gegegegex3", "", { { "phrase-gege-x3", 0, 208433, 4.0 } } },
            { 14, "gege-modulations", "", { { "phrase-gege-modulations", 0, 237199, 7.0 } } },
            { 15, "gegegegex4", "", { { "phrase-gege-x2", 0, 95180, 0.0 } } },
            { 16, "kath-R2", "ke", { { "ke-2", 180, 87514, 0.0 } } },
            { 17, "kath-R", "ke", { { "ke-1", 475, 106175, 0.0 } } },
            { 18, "te2", "te", { { "te-1", 579, 90191, 0.0 } } },
            { 19, "te1", "te", { { "te-2", 0, 22264, 0.0 } } },
            { 20, "tin2", "tin", { { "tin-2", 0, 210576, 5.0 } } },
            { 21, "tā2", "ta", { { "ta-1", 0, 157789, 8.0 } } },
            { 22, "tak", "tak", { { "tak", 0, 97859, 4.0 } } },
            { 23, "te3", "te", { { "te-3", 159, 38063, 0.0 } } },
            { 24, "tu2", "tu", { { "tu-1", 0, 250525, 10.0 } } },
            { 25, "ge-glide1", "", { { "ge-meend", 0, 27323, 7.0 } } },
            { 26, "ge2", "ge", { { "ge-1", 0, 67193, 9.0 } } },
            { 27, "gā4", "ge", { { "ge-2", 0, 54090, 13.0 } } },
            { 28, "tu1", "tu", { { "tu-2", 251, 145295, 7.0 } } },
            { 29, "tin1", "tin", { { "tin-1", 87, 157437, 9.0 } } },
            { 30, "tā1", "ta", { { "ta-2", 397, 110767, 3.0 } } },
            { 31, "dhā", "dha", { { "dha-a", 529, 115199, 7.0 } } },
            { 32, "dha (tā + ge)", "dha", { { "ta-1", 0, 157789, 4.0 }, { "ge-1", 0, 67193, 6.0 } } },
            { 33, "dha 2 (tā + gā)", "dha", { { "ta-2", 397, 110767, 3.0 }, { "ge-2", 0, 54090, 9.0 } } },
            { 34, "dhin (tin + ge)", "dhin", { { "tin-1", 87, 157437, 9.0 }, { "ge-1", 0, 67193, 6.0 } } },
            { 35, "dhin 2 (tin + gā)", "dhin", { { "tin-2", 0, 210576, 5.0 }, { "ge-2", 0, 54090, 9.0 } } },
            { 36, "dhi (te + ge)", "dhi", { { "te-1", 579, 90191, 0.0 }, { "ge-1", 0, 67193, 6.0 } } },
            { 37, "dhi 2 (te + gā)", "dhi", { { "te-3", 159, 38063, 0.0 }, { "ge-2", 0, 54090, 9.0 } } },
            { 38, "dhun (tu + ge)", "dhun", { { "tu-1", 0, 250525, 10.0 }, { "ge-1", 0, 67193, 6.0 } } },
            { 39, "kran (ke + tā)", "kran", { { "ke-1", 475, 106175, 0.0 }, { "ta-1", 0, 157789, 4.0 } } },
            { 40, "Sa: dāyāṅ (C♯)", "", { { "sa-dayan-c-sharp" } } },
            { 41, "Sa: bāyāṅ (F♯)", "", { { "sa-bayan-f-sharp" } } },
            { 42, "phrase: ge modulations (whole)", "", { { "phrase-gege-modulations" } } },
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
        auto get = [&] (const char* f)
        {
            const auto path = folder + "/" + f + ".wav";
            auto& s = loaded[f];
            if (s == nullptr)
                s = load (path);
            return std::pair { s, path };
        };
        for (const auto& p : pads())
        {
            // A pad's own sounds, else its stand-ins (the dhā pad until its recording comes).
            auto sounds = p.sounds;
            for (const auto& x : p.sounds)
                if (get (x.file).first == nullptr)
                {
                    if (missing != nullptr)
                        missing->push_back (folder + "/" + x.file + ".wav");
                    sounds = p.fallback;
                }
            int layer = 0;
            for (const auto& x : sounds)
            {
                const auto [s, path] = get (x.file);
                if (s == nullptr || layer >= slicer::maxLayers)
                    continue;
                slicer::placeSound (k, p.pad, layer, s, path);
                auto& l = k.pads[(size_t) p.pad].layers[(size_t) layer++];
                l.start = std::clamp<Frame> (x.start, 0, s->length());
                l.end = x.end < 0 ? s->length() : std::clamp<Frame> (x.end, l.start, s->length());
                l.gainDb = x.gainDb;
            }
            auto& pad = k.pads[(size_t) p.pad];
            pad.name = p.name;
            pad.colour = *p.bol == 0 ? 9 : p.pad >= 32 ? 3 : 6;
            // Strokes ring out; the open ones ring a while, so a stroke stops the last one on its drum.
            pad.release = 0.08;
            const std::string b = p.bol;
            pad.choke = b == "ge" || b == "ke" ? 2 : (b == "ta" || b == "tin" || b == "te" || b == "tu" || b == "tak" || b == "trika") ? 1 : 0;
        }
        return k;
    }
}
