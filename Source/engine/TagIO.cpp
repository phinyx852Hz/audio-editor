// SPDX-License-Identifier: AGPL-3.0-only
#include "TagIO.h"

#include "../core/Metadata.h"
#include "../core/Mp4Xmp.h"
#include "../core/Xmp.h"

#include <taglib/apefile.h>
#include <taglib/apeitem.h>
#include <taglib/apetag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/chapterframe.h>
#include <taglib/commentsframe.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v1tag.h>
#include <taglib/id3v2framefactory.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mpcfile.h>
#include <taglib/mpegfile.h>
#include <taglib/opusfile.h>
#include <taglib/privateframe.h>
#include <taglib/speexfile.h>
#include <taglib/tableofcontentsframe.h>
#include <taglib/taglib.h>
#include <taglib/tbytevectorstream.h>
#include <taglib/textidentificationframe.h>
#include <taglib/tpropertymap.h>
#include <taglib/trueaudiofile.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/urllinkframe.h>
#include <taglib/vorbisfile.h>
#include <taglib/wavpackfile.h>
#include <taglib/xiphcomment.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

namespace tagio
{
namespace
{
    using TagLib::ByteVector;
    using TagLib::String;
    using TagLib::StringList;

    String u (const std::string& s) { return String (s, String::UTF8); }
    std::string s8 (const String& s) { return s.to8Bit (true); }
    ByteVector bytes (const std::string& s) { return ByteVector (s.data(), (unsigned int) s.size()); }
    std::string fromBytes (const ByteVector& b) { return std::string (b.data(), b.size()); }

    std::string join (const StringList& list)
    {
        std::string out;
        for (const auto& item : list)
            if (! item.isEmpty())
                out += (out.empty() ? "" : "\n") + s8 (item);
        return out;
    }

    StringList split (const std::string& value)
    {
        StringList out;
        size_t start = 0;
        for (;;)
        {
            const auto nl = value.find ('\n', start);
            const auto item = value.substr (start, nl == std::string::npos ? std::string::npos : nl - start);
            if (! item.empty())
                out.append (u (item));
            if (nl == std::string::npos)
                break;
            start = nl + 1;
        }
        return out;
    }

    std::string extensionOf (const std::string& path)
    {
        auto ext = std::filesystem::path (path).extension().string();
        if (! ext.empty())
            ext.erase (0, 1);
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return ext;
    }

    using Values = std::vector<std::pair<std::string, std::string>>;

    //==============================================================================
    // ID3v2
    void readId3Tag (const TagLib::ID3v2::Tag& tag, Tags& tags, std::vector<Marker>* chapters, double sampleRate)
    {
        using namespace TagLib::ID3v2;
        Values values;
        std::vector<meta::Picture> pictures = meta::pictures (tags);
        bool firstComment = true;

        for (auto* frame : tag.frameList())
        {
            const auto id = fromBytes (frame->frameID());
            if (auto* pic = dynamic_cast<AttachedPictureFrame*> (frame))
            {
                meta::Picture p;
                p.type = (int) pic->type();
                p.mime = s8 (pic->mimeType());
                p.description = s8 (pic->description());
                p.data = fromBytes (pic->picture());
                if (p.mime.empty() || p.mime.find ('/') == std::string::npos)
                    p.mime = meta::mimeOfImage (p.data);
                pictures.push_back (std::move (p));
            }
            else if (auto* priv = dynamic_cast<PrivateFrame*> (frame))
            {
                if (priv->owner() == "XMP")
                {
                    std::string error;
                    xmp::read (fromBytes (priv->data()), tags, error);
                }
            }
            else if (auto* user = dynamic_cast<UserTextIdentificationFrame*> (frame))
            {
                auto list = user->fieldList();
                if (! list.isEmpty())
                    list.erase (list.begin());
                values.emplace_back ("TXXX:" + s8 (user->description()), join (list));
            }
            else if (auto* comment = dynamic_cast<CommentsFrame*> (frame))
            {
                const auto desc = s8 (comment->description());
                values.emplace_back (desc.empty() && firstComment ? std::string ("COMM") : "COMM:" + desc, s8 (comment->text()));
                firstComment = firstComment && ! desc.empty();
            }
            else if (auto* lyrics = dynamic_cast<UnsynchronizedLyricsFrame*> (frame))
                values.emplace_back ("USLT", s8 (lyrics->text()));
            else if (auto* userUrl = dynamic_cast<UserUrlLinkFrame*> (frame))
                values.emplace_back ("WXXX:" + s8 (userUrl->description()), s8 (userUrl->url()));
            else if (auto* url = dynamic_cast<UrlLinkFrame*> (frame))
                values.emplace_back (id, s8 (url->url()));
            else if (auto* text = dynamic_cast<TextIdentificationFrame*> (frame))
                values.emplace_back (id, id == "TCON" ? s8 (tag.genre()) : join (text->fieldList()));
            else if (auto* chap = dynamic_cast<ChapterFrame*> (frame); chap != nullptr && chapters != nullptr && sampleRate > 0.0)
            {
                Marker m;
                m.start = (::Frame) std::llround (chap->startTime() * sampleRate / 1000.0);
                m.end = m.start;
                m.name = "Chapter " + std::to_string (chapters->size() + 1);
                for (auto* sub : chap->embeddedFrameList ("TIT2"))
                    m.name = s8 (sub->toString());
                m.id = (int) chapters->size() + 1;
                chapters->push_back (m);
            }
        }
        meta::absorb (tags, meta::Schema::id3, values);
        meta::setPictures (tags, pictures);
    }

    void writeId3Tag (TagLib::ID3v2::Tag& tag, const Tags& tags, const std::vector<Marker>& markers, double sampleRate,
                      const Options& options)
    {
        using namespace TagLib::ID3v2;
        const auto encoding = options.id3Version == 3 ? String::UTF16 : String::UTF8;

        // Frames PHINYX writes go; others (play counts, ratings...) stay.
        std::vector<TagLib::ID3v2::Frame*> remove;
        for (auto* frame : tag.frameList())
        {
            const auto id = fromBytes (frame->frameID());
            const bool privXmp = dynamic_cast<PrivateFrame*> (frame) != nullptr && static_cast<PrivateFrame*> (frame)->owner() == "XMP";
            if (id[0] == 'T' || id[0] == 'W' || id == "COMM" || id == "USLT" || id == "APIC" || id == "CHAP" || id == "CTOC" || privXmp)
                remove.push_back (frame);
        }
        for (auto* f : remove)
            tag.removeFrame (f, true);

        auto values = meta::valuesFor (tags, meta::Schema::id3);
        if (std::none_of (values.begin(), values.end(), [] (const auto& v) { return v.first == "TSSE"; }) && ! options.software.empty())
            values.emplace_back ("TSSE", options.software);

        for (const auto& [id, value] : values)
        {
            if (id.compare (0, 5, "TXXX:") == 0)
            {
                auto* f = new UserTextIdentificationFrame (encoding);
                f->setDescription (u (id.substr (5)));
                f->setText (split (value));
                tag.addFrame (f);
            }
            else if (id == "COMM" || id.compare (0, 5, "COMM:") == 0)
            {
                auto* f = new CommentsFrame (encoding);
                f->setLanguage ("eng");
                f->setDescription (id.size() > 5 ? u (id.substr (5)) : String());
                f->setText (u (value));
                tag.addFrame (f);
            }
            else if (id == "USLT")
            {
                auto* f = new UnsynchronizedLyricsFrame (encoding);
                f->setLanguage ("eng");
                f->setText (u (value));
                tag.addFrame (f);
            }
            else if (id.compare (0, 5, "WXXX:") == 0)
            {
                auto* f = new UserUrlLinkFrame (encoding);
                f->setDescription (u (id.substr (5)));
                f->setUrl (u (value));
                tag.addFrame (f);
            }
            else if (id.size() == 4 && id[0] == 'W')
            {
                auto* f = new UrlLinkFrame (bytes (id));
                f->setUrl (u (value));
                tag.addFrame (f);
            }
            else if (id.size() == 4 && id[0] == 'T')
            {
                auto* f = new TextIdentificationFrame (bytes (id), encoding);
                f->setText (split (value));
                tag.addFrame (f);
            }
        }

        for (const auto& p : meta::pictures (tags))
        {
            auto* f = new AttachedPictureFrame();
            f->setTextEncoding (encoding);
            f->setMimeType (u (p.mime.empty() ? meta::mimeOfImage (p.data) : p.mime));
            f->setType ((AttachedPictureFrame::Type) std::clamp (p.type, 0, 20));
            f->setDescription (u (p.description));
            f->setPicture (bytes (p.data));
            tag.addFrame (f);
        }

        if (options.xmp)
            if (const auto packet = xmp::write (tags, options.software, false); ! packet.empty())
            {
                auto* f = new PrivateFrame();
                f->setOwner ("XMP");
                f->setData (bytes (packet));
                tag.addFrame (f);
            }

        if (options.chapters && sampleRate > 0.0 && ! markers.empty())
        {
            TagLib::ByteVectorList children;
            for (size_t i = 0; i < markers.size(); ++i)
            {
                const auto& m = markers[i];
                const auto start = (unsigned int) std::llround ((double) m.start * 1000.0 / sampleRate);
                const auto end = (unsigned int) std::llround ((double) (m.isRegion() ? m.end : (i + 1 < markers.size() ? markers[i + 1].start : m.start))
                                                              * 1000.0 / sampleRate);
                const auto element = bytes ("chp" + std::to_string (i));
                auto* title = new TextIdentificationFrame ("TIT2", encoding);
                title->setText (u (m.name));
                TagLib::ID3v2::FrameList embedded;
                embedded.append (title);
                tag.addFrame (new ChapterFrame (element, start, std::max (start, end), 0xffffffffu, 0xffffffffu, embedded));
                children.append (element);
            }
            auto* toc = new TableOfContentsFrame ("toc", children);
            toc->setIsTopLevel (true);
            toc->setIsOrdered (true);
            tag.addFrame (toc);
        }
    }

    TagLib::ID3v2::Version id3Version (const Options& o) { return o.id3Version == 3 ? TagLib::ID3v2::v3 : TagLib::ID3v2::v4; }

    //==============================================================================
    // Vorbis comments
    void readXiph (TagLib::Ogg::XiphComment& c, Tags& tags)
    {
        Values values;
        for (const auto& [key, list] : c.fieldListMap())
        {
            const auto k = s8 (key);
            if (k == "METADATA_BLOCK_PICTURE" || k == "COVERART" || k == "COVERARTMIME")
                continue;
            if (k.size() >= 10 && k.compare (0, 7, "CHAPTER") == 0 && std::isdigit ((unsigned char) k[7]))
                continue;   // chapters, read as markers
            // The comment is called COMMENT by most programs, DESCRIPTION by the specification.
            values.emplace_back (k, join (list));
        }
        meta::absorb (tags, meta::Schema::vorbis, values);

        auto pictures = meta::pictures (tags);
        for (auto* p : c.pictureList())
            pictures.push_back ({ (int) p->type(), s8 (p->mimeType()), s8 (p->description()), fromBytes (p->data()) });
        meta::setPictures (tags, pictures);
    }

    TagLib::FLAC::Picture* flacPicture (const meta::Picture& p)
    {
        auto* pic = new TagLib::FLAC::Picture();
        pic->setType ((TagLib::FLAC::Picture::Type) std::clamp (p.type, 0, 20));
        pic->setMimeType (u (p.mime.empty() ? meta::mimeOfImage (p.data) : p.mime));
        pic->setDescription (u (p.description));
        pic->setData (bytes (p.data));
        return pic;
    }

    // Vorbis chapter comments: CHAPTER001=00:01:02.500, CHAPTER001NAME=Verse.
    void writeXiphChapters (TagLib::Ogg::XiphComment& c, const std::vector<Marker>& markers, double sampleRate)
    {
        for (size_t i = 0; i < markers.size() && i < 999 && sampleRate > 0.0; ++i)
        {
            const auto ms = (long long) std::llround ((double) markers[i].start * 1000.0 / sampleRate);
            char key[16], time[32];
            std::snprintf (key, sizeof key, "CHAPTER%03d", (int) i + 1);
            std::snprintf (time, sizeof time, "%02lld:%02lld:%02lld.%03lld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
            c.addField (key, time, true);
            c.addField (String (key) + "NAME", u (markers[i].name), true);
        }
    }

    void writeXiph (TagLib::Ogg::XiphComment& c, const Tags& tags, const Options& options, bool pictures)
    {
        c.removeAllFields();
        c.removeAllPictures();
        auto values = meta::valuesFor (tags, meta::Schema::vorbis);
        if (std::none_of (values.begin(), values.end(), [] (const auto& v) { return v.first == "ENCODER"; }) && ! options.software.empty())
            values.emplace_back ("ENCODER", options.software);
        for (const auto& [key, value] : values)
            for (const auto& item : split (value))
                c.addField (u (key), item, false);
        if (pictures)
            for (const auto& p : meta::pictures (tags))
                c.addPicture (flacPicture (p));
    }

    //==============================================================================
    // MP4
    const std::set<std::string> mp4Ints { "tmpo", "rtng", "stik", "tvsn", "tves", "hdvd", "akID", "sfID", "cnID", "geID", "atID", "plID", "cmID" };
    const std::set<std::string> mp4Bools { "cpil", "pgap", "pcst" };

    void readMp4 (TagLib::MP4::Tag& tag, Tags& tags)
    {
        Values values;
        auto pictures = meta::pictures (tags);
        for (const auto& [key, item] : tag.itemMap())
        {
            const auto k = s8 (key);
            if (k == "covr")
            {
                for (const auto& art : item.toCoverArtList())
                    pictures.push_back ({ 3, art.format() == TagLib::MP4::CoverArt::PNG ? "image/png" : "image/jpeg", {}, fromBytes (art.data()) });
            }
            else if (k == "trkn" || k == "disk")
            {
                const auto pair = item.toIntPair();
                values.emplace_back (k, std::to_string (pair.first) + (pair.second > 0 ? "/" + std::to_string (pair.second) : ""));
            }
            else if (mp4Ints.count (k) > 0)
                values.emplace_back (k, std::to_string (item.toInt()));
            else if (mp4Bools.count (k) > 0)
                values.emplace_back (k, item.toBool() ? "1" : "0");
            else if (! item.toStringList().isEmpty())
                values.emplace_back (k, join (item.toStringList()));
        }
        meta::absorb (tags, meta::Schema::mp4, values);
        meta::setPictures (tags, pictures);
    }

    void writeMp4 (TagLib::MP4::Tag& tag, const Tags& tags, const Options& options)
    {
        // Items PHINYX reads as text, numbers or pictures go; others stay.
        std::vector<String> remove;
        for (const auto& [key, item] : tag.itemMap())
        {
            const auto k = s8 (key);
            if (k == "covr" || k == "trkn" || k == "disk" || mp4Ints.count (k) > 0 || mp4Bools.count (k) > 0 || ! item.toStringList().isEmpty())
                remove.push_back (key);
        }
        for (const auto& k : remove)
            tag.removeItem (k);

        auto values = meta::valuesFor (tags, meta::Schema::mp4);
        if (std::none_of (values.begin(), values.end(), [] (const auto& v) { return v.first == "\xC2\xA9too"; }) && ! options.software.empty())
            values.emplace_back ("\xC2\xA9too", options.software);
        for (const auto& [key, value] : values)
        {
            const auto k = u (key);
            if (key == "trkn" || key == "disk")
                tag.setItem (k, TagLib::MP4::Item (std::atoi (meta::numberPart (value).c_str()), std::atoi (meta::totalPart (value).c_str())));
            else if (mp4Ints.count (key) > 0)
                tag.setItem (k, TagLib::MP4::Item ((int) std::lround (std::atof (value.c_str()))));
            else if (mp4Bools.count (key) > 0)
                tag.setItem (k, TagLib::MP4::Item (value == "1" || value == "true" || value == "yes"));
            else
                tag.setItem (k, TagLib::MP4::Item (split (value)));
        }

        TagLib::MP4::CoverArtList art;
        for (const auto& p : meta::pictures (tags))
            art.append (TagLib::MP4::CoverArt (meta::mimeOfImage (p.data) == "image/png" ? TagLib::MP4::CoverArt::PNG
                                                                                       : TagLib::MP4::CoverArt::JPEG,
                                               bytes (p.data)));
        if (! art.isEmpty())
            tag.setItem ("covr", TagLib::MP4::Item (art));
    }

    //==============================================================================
    // APEv2
    const char* apeCoverName (int type) { return type == 4 ? "Cover Art (Back)" : type == 3 ? "Cover Art (Front)" : "Cover Art (Other)"; }

    void readApe (TagLib::APE::Tag& tag, Tags& tags)
    {
        Values values;
        auto pictures = meta::pictures (tags);
        for (const auto& [mapKey, item] : tag.itemListMap())
        {
            const auto key = s8 (item.key());
            if (item.type() == TagLib::APE::Item::Binary)
            {
                if (key.compare (0, 9, "Cover Art") == 0)
                {
                    const auto data = fromBytes (item.binaryData());
                    const auto zero = data.find ('\0');
                    meta::Picture p;
                    p.type = key.find ("Back") != std::string::npos ? 4 : key.find ("Front") != std::string::npos ? 3 : 0;
                    p.data = zero == std::string::npos ? data : data.substr (zero + 1);
                    p.mime = meta::mimeOfImage (p.data);
                    p.description = zero == std::string::npos ? std::string() : data.substr (0, zero);
                    pictures.push_back (std::move (p));
                }
                continue;
            }
            values.emplace_back (key, join (item.values()));
        }
        meta::absorb (tags, meta::Schema::ape, values);
        meta::setPictures (tags, pictures);
    }

    void writeApe (TagLib::APE::Tag& tag, const Tags& tags)
    {
        std::vector<String> remove;
        for (const auto& [mapKey, item] : tag.itemListMap())
            if (item.type() == TagLib::APE::Item::Text || s8 (item.key()).compare (0, 9, "Cover Art") == 0)
                remove.push_back (item.key());
        for (const auto& k : remove)
            tag.removeItem (k);
        for (const auto& [key, value] : meta::valuesFor (tags, meta::Schema::ape))
            tag.setItem (u (key), TagLib::APE::Item (u (key), split (value)));
        for (const auto& p : meta::pictures (tags))
        {
            const auto name = std::string (apeCoverName (p.type));
            const auto file = (p.description.empty() ? std::string ("cover") : p.description)
                              + (meta::mimeOfImage (p.data) == "image/png" ? ".png" : ".jpg");
            tag.setItem (u (name), TagLib::APE::Item (u (name), bytes (file + '\0' + p.data), true));
        }
    }

    //==============================================================================
    void readProperties (const TagLib::PropertyMap& map, Tags& tags)
    {
        Values values;
        for (const auto& [key, list] : map)
            values.emplace_back (s8 (key), join (list));
        meta::absorb (tags, meta::Schema::vorbis, values);
    }

    TagLib::PropertyMap propertiesFor (const Tags& tags)
    {
        TagLib::PropertyMap map;
        for (const auto& [key, value] : meta::valuesFor (tags, meta::Schema::vorbis))
            map.insert (u (key), split (value));
        return map;
    }

    void readSidecar (const std::string& path, Tags& tags)
    {
        std::ifstream in (sidecarPath (path), std::ios::binary);
        if (! in)
            return;
        const std::string xml ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        std::string error;
        xmp::read (xml, tags, error);
    }

    bool writeSidecar (const std::string& path, const Tags& tags, const Options& options, std::string& error)
    {
        const auto side = sidecarPath (path);
        const auto packet = options.xmp ? xmp::write (tags, options.software, false) : std::string();
        if (packet.empty())
            return true;
        std::ofstream out (side, std::ios::binary | std::ios::trunc);
        out << packet;
        if (! out)
            return error = "Could not write " + side, false;
        return true;
    }
}

//==============================================================================
std::string sidecarPath (const std::string& path)
{
    return std::filesystem::path (path).replace_extension (".xmp").string();
}

bool handles (const std::string& path)
{
    const auto ext = extensionOf (path);
    return ! ext.empty() && ext != "wav" && ext != "wave" && ext != "bwf" && ext != "rf64" && ext != "w64" && ext != "aif"
           && ext != "aiff" && ext != "aifc";
}

void readId3 (const std::string& data, Tags& tags)
{
    // TagLib reads an ID3v2 tag from the start of an MPEG stream; the bytes are such a start.
    TagLib::ByteVectorStream stream (bytes (data));
#if TAGLIB_MAJOR_VERSION >= 2
    TagLib::MPEG::File file (&stream, false);
#else
    TagLib::MPEG::File file (&stream, TagLib::ID3v2::FrameFactory::instance(), false);
#endif
    if (auto* tag = file.ID3v2Tag (false))
        readId3Tag (*tag, tags, nullptr, 0.0);
}

std::string renderId3 (const Tags& tags, const Options& options)
{
    TagLib::ID3v2::Tag tag;
    writeId3Tag (tag, tags, {}, 0.0, options);
    if (tag.frameList().isEmpty())
        return {};
    return fromBytes (tag.render (id3Version (options)));
}

void read (const std::string& path, Tags& tags, std::vector<Marker>* chapters, double sampleRate)
{
    const auto ext = extensionOf (path);
    const auto name = path.c_str();
    bool sidecar = true;

    if (ext == "mp3" || ext == "mp2" || ext == "mpga" || ext == "mpa")
    {
        TagLib::MPEG::File f (name, false);
        if (auto* t = f.ID3v2Tag (false))
            readId3Tag (*t, tags, chapters, sampleRate);
        if (auto* t1 = f.ID3v1Tag (false))
        {
            // ID3v1 only fills what ID3v2 left empty.
            for (const auto& [key, value] : std::initializer_list<std::pair<const char*, String>> {
                     { tag::title, t1->title() }, { tag::artist, t1->artist() }, { tag::album, t1->album() },
                     { tag::comment, t1->comment() }, { tag::genre, t1->genre() } })
                if (! value.isEmpty() && tags[key].empty())
                    tags[key] = s8 (value);
            if (t1->year() > 0 && tags[tag::date].empty())
                tags[tag::date] = std::to_string (t1->year());
            if (t1->track() > 0 && tags[tag::track].empty())
                tags[tag::track] = std::to_string (t1->track());
            std::erase_if (tags, [] (const auto& kv) { return kv.second.empty(); });
        }
        sidecar = false;
    }
    else if (ext == "flac")
    {
        TagLib::FLAC::File f (name, false);
        if (auto* x = f.xiphComment (false))
            readXiph (*x, tags);
        auto pictures = meta::pictures (tags);
        for (auto* p : f.pictureList())
            pictures.push_back ({ (int) p->type(), s8 (p->mimeType()), s8 (p->description()), fromBytes (p->data()) });
        meta::setPictures (tags, pictures);
    }
    else if (ext == "ogg" || ext == "oga")
    {
        TagLib::Ogg::Vorbis::File f (name, false);
        if (auto* x = f.tag())
            readXiph (*x, tags);
    }
    else if (ext == "opus")
    {
        TagLib::Ogg::Opus::File f (name, false);
        if (auto* x = f.tag())
            readXiph (*x, tags);
    }
    else if (ext == "spx")
    {
        TagLib::Ogg::Speex::File f (name, false);
        if (auto* x = f.tag())
            readXiph (*x, tags);
    }
    else if (ext == "m4a" || ext == "m4b" || ext == "mp4" || ext == "m4r" || ext == "alac" || ext == "mov" || ext == "3gp")
    {
        TagLib::MP4::File f (name, false);
        if (auto* t = f.tag())
            readMp4 (*t, tags);
        if (const auto x = mp4xmp::read (path); ! x.empty())
        {
            std::string error;
            xmp::read (x, tags, error);
        }
        sidecar = false;
    }
    else if (ext == "wv")
    {
        TagLib::WavPack::File f (name, false);
        if (auto* t = f.APETag (false))
            readApe (*t, tags);
    }
    else if (ext == "ape")
    {
        TagLib::APE::File f (name, false);
        if (auto* t = f.APETag (false))
            readApe (*t, tags);
    }
    else if (ext == "mpc" || ext == "mp+")
    {
        TagLib::MPC::File f (name, false);
        if (auto* t = f.APETag (false))
            readApe (*t, tags);
    }
    else if (ext == "tta")
    {
        TagLib::TrueAudio::File f (name, false);
        if (auto* t = f.ID3v2Tag (false))
            readId3Tag (*t, tags, chapters, sampleRate);
    }
    else
    {
        TagLib::FileRef f (name, false);
        if (! f.isNull())
            readProperties (f.file()->properties(), tags);
    }

    if (sidecar)
        readSidecar (path, tags);
}

bool write (const std::string& path, const Tags& tags, const std::vector<Marker>& markers, double sampleRate,
            const Options& options, std::string& error)
{
    const auto ext = extensionOf (path);
    const auto name = path.c_str();
    bool ok = true, sidecar = true;
    const auto fail = [&] { error = "Could not write the tags of " + path; return false; };

    if (ext == "mp3" || ext == "mp2" || ext == "mpga" || ext == "mpa")
    {
        TagLib::MPEG::File f (name, false);
        if (! f.isValid())
            return fail();
        int which = 0;
        if (options.id3)
        {
            writeId3Tag (*f.ID3v2Tag (true), tags, markers, sampleRate, options);
            which |= TagLib::MPEG::File::ID3v2;
            if (options.id3v1)
            {
                // ID3v1 holds the basics, cut to its field sizes.
                auto* v1 = f.ID3v1Tag (true);
                const auto value = [&] (const char* key) { const auto it = tags.find (key); return it == tags.end() ? String() : u (it->second); };
                v1->setTitle (value (tag::title));
                v1->setArtist (value (tag::artist));
                v1->setAlbum (value (tag::album));
                v1->setComment (value (tag::comment));
                v1->setGenre (value (tag::genre));
                v1->setYear ((unsigned int) std::max (0, std::atoi (value (tag::date).to8Bit (true).substr (0, 4).c_str())));
                v1->setTrack ((unsigned int) std::max (0, std::atoi (meta::numberPart (value (tag::track).to8Bit (true)).c_str())));
                which |= TagLib::MPEG::File::ID3v1;
            }
        }
        ok = which == 0 ? f.strip() : f.save (which, TagLib::File::StripOthers, id3Version (options), TagLib::File::DoNotDuplicate);
        sidecar = false;
    }
    else if (ext == "flac")
    {
        TagLib::FLAC::File f (name, false);
        if (! f.isValid())
            return fail();
        f.removePictures();
        if (options.vorbis)
        {
            writeXiph (*f.xiphComment (true), tags, options, false);
            for (const auto& p : meta::pictures (tags))
                f.addPicture (flacPicture (p));
        }
        else if (auto* x = f.xiphComment (false))
            x->removeAllFields();
        // ID3 in FLAC is not standard: it goes.
        f.strip (TagLib::FLAC::File::ID3v1 | TagLib::FLAC::File::ID3v2);
        ok = f.save();
    }
    else if (ext == "ogg" || ext == "oga" || ext == "opus" || ext == "spx")
    {
        std::unique_ptr<TagLib::Ogg::File> f;
        TagLib::Ogg::XiphComment* x = nullptr;
        if (ext == "opus")
        {
            auto* o = new TagLib::Ogg::Opus::File (name, false);
            x = o->tag();
            f.reset (o);
        }
        else if (ext == "spx")
        {
            auto* sp = new TagLib::Ogg::Speex::File (name, false);
            x = sp->tag();
            f.reset (sp);
        }
        else
        {
            auto* v = new TagLib::Ogg::Vorbis::File (name, false);
            x = v->tag();
            f.reset (v);
        }
        if (! f->isValid() || x == nullptr)
            return fail();
        if (options.vorbis)
        {
            writeXiph (*x, tags, options, true);
            if (options.chapters)
                writeXiphChapters (*x, markers, sampleRate);
        }
        else
            x->removeAllFields(), x->removeAllPictures();
        ok = f->save();
    }
    else if (ext == "m4a" || ext == "m4b" || ext == "mp4" || ext == "m4r" || ext == "alac" || ext == "mov" || ext == "3gp")
    {
        {
            TagLib::MP4::File f (name, false);
            if (! f.isValid() || f.tag() == nullptr)
                return fail();
            if (options.mp4)
                writeMp4 (*f.tag(), tags, options);
            else
                writeMp4 (*f.tag(), Tags(), Options());
            ok = f.save();
        }
        // After TagLib, which rewrites the boxes before the audio.
        if (ok && ! mp4xmp::write (path, options.xmp ? xmp::write (tags, options.software, false) : std::string(), error))
            return false;
        sidecar = false;
    }
    else if (ext == "wv" || ext == "ape" || ext == "mpc" || ext == "mp+")
    {
        std::unique_ptr<TagLib::File> f;
        TagLib::APE::Tag* t = nullptr;
        if (ext == "wv")
        {
            auto* w = new TagLib::WavPack::File (name, false);
            t = w->APETag (true);
            f.reset (w);
        }
        else if (ext == "ape")
        {
            auto* a = new TagLib::APE::File (name, false);
            t = a->APETag (true);
            f.reset (a);
        }
        else
        {
            auto* m = new TagLib::MPC::File (name, false);
            t = m->APETag (true);
            f.reset (m);
        }
        if (! f->isValid() || t == nullptr)
            return fail();
        writeApe (*t, options.ape ? tags : Tags());
        ok = f->save();
    }
    else if (ext == "tta")
    {
        TagLib::TrueAudio::File f (name, false);
        if (! f.isValid())
            return fail();
        if (options.id3)
            writeId3Tag (*f.ID3v2Tag (true), tags, {}, 0.0, options);
        ok = f.save();
    }
    else
    {
        TagLib::FileRef f (name, false);
        if (f.isNull())
            return true;   // a format without tags
        f.file()->setProperties (propertiesFor (tags));
        ok = f.save();
    }

    if (! ok)
        return fail();
    return ! sidecar || writeSidecar (path, tags, options, error);
}

std::vector<std::pair<std::string, std::string>> rawItems (const std::string& path)
{
    std::vector<std::pair<std::string, std::string>> out;
    const auto shorten = [] (std::string v)
    {
        std::replace (v.begin(), v.end(), '\n', ' ');
        return v.size() > 200 ? v.substr (0, 200) + "..." : v;
    };
    const auto ext = extensionOf (path);

    const auto listId3 = [&] (TagLib::ID3v2::Tag* t, const char* label)
    {
        if (t == nullptr)
            return;
        for (auto* frame : t->frameList())
        {
            const auto id = fromBytes (frame->frameID());
            const auto size = frame->size();
            out.emplace_back (std::string (label) + " " + id,
                              id == "APIC" || id == "PRIV" || id == "GEOB" ? std::to_string (size) + " bytes" : shorten (s8 (frame->toString())));
        }
    };

    if (ext == "mp3" || ext == "mp2" || ext == "mpga" || ext == "mpa")
    {
        TagLib::MPEG::File f (path.c_str(), false);
        listId3 (f.ID3v2Tag (false), "ID3v2");
        if (f.hasID3v1Tag())
            out.emplace_back ("ID3v1", "present");
        if (f.hasAPETag())
            out.emplace_back ("APEv2", "present (not written by PHINYX)");
        return out;
    }
    if (ext == "tta")
    {
        TagLib::TrueAudio::File f (path.c_str(), false);
        listId3 (f.ID3v2Tag (false), "ID3v2");
        return out;
    }
    if (ext == "m4a" || ext == "m4b" || ext == "mp4" || ext == "m4r" || ext == "alac" || ext == "mov" || ext == "3gp")
    {
        TagLib::MP4::File f (path.c_str(), false);
        if (f.tag() != nullptr)
            for (const auto& [key, item] : f.tag()->itemMap())
                out.emplace_back ("MP4 " + s8 (key), s8 (key) == "covr" ? std::to_string (item.toCoverArtList().size()) + " picture(s)"
                                                                         : shorten (join (item.toStringList())));
        if (! mp4xmp::read (path).empty())
            out.emplace_back ("MP4 uuid", "XMP packet");
        return out;
    }

    TagLib::FileRef f (path.c_str(), false);
    if (f.isNull())
        return out;
    const auto props = f.file()->properties();
    for (const auto& [key, list] : props)
        out.emplace_back (s8 (key), shorten (join (list)));
    for (const auto& item : props.unsupportedData())
        out.emplace_back (s8 (item), "(kept, not shown)");
    if (ext == "flac")
    {
        TagLib::FLAC::File flac (path.c_str(), false);
        for (auto* p : flac.pictureList())
            out.emplace_back ("FLAC PICTURE", std::string (meta::pictureTypeName ((int) p->type())) + ", " + std::to_string (p->data().size()) + " bytes");
    }
    return out;
}
}
