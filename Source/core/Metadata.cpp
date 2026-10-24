// SPDX-License-Identifier: AGPL-3.0-only
#include "Metadata.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace meta
{
namespace
{
    struct Catalog
    {
        std::vector<Field> list;
        Schema schema = Schema::common;
        std::string group;

        Catalog& in (Schema s, std::string g = {}) { schema = s; group = std::move (g); return *this; }

        Catalog& add (std::string id, std::string label, std::string common = {}, Follow follow = Follow::same,
                      Kind kind = Kind::text, int maxLength = 0)
        {
            Field f;
            f.schema = schema;
            f.id = std::move (id);
            f.key = keyFor (schema, f.id);
            f.label = std::move (label);
            f.group = group;
            f.common = std::move (common);
            f.follow = follow;
            f.kind = kind;
            f.maxLength = maxLength;
            list.push_back (std::move (f));
            return *this;
        }

        // XMP property: id is "prefix:name".
        Catalog& xmp (std::string id, std::string label, XmpType type = XmpType::simple, std::string common = {},
                      Follow follow = Follow::same)
        {
            add (std::move (id), std::move (label), std::move (common), follow,
                 type == XmpType::bag || type == XmpType::seq ? Kind::lines : Kind::text);
            list.back().xmpType = type;
            return *this;
        }
    };

    Catalog build()
    {
        Catalog c;
        using F = Follow;
        using K = Kind;
        using X = XmpType;

        // RIFF INFO list and the DISP chunk (WAV).
        c.in (Schema::riff, "RIFF INFO")
            .add ("DISP", "Display Title", tag::title)
            .add ("INAM", "Name (Title)", tag::title)
            .add ("IART", "Artist", tag::artist)
            .add ("IPRD", "Product (Album)", tag::album)
            .add ("IGNR", "Genre", tag::genre)
            .add ("ICRD", "Creation Date", tag::date)
            .add ("ITRK", "Track Number", tag::track)
            .add ("ICMT", "Comments", tag::comment, F::same, K::lines)
            .add ("ICOP", "Copyright", tag::copyright)
            .add ("IENG", "Engineer", tag::engineer)
            .add ("ITCH", "Technician")
            .add ("IKEY", "Keywords", tag::keywords)
            .add ("ISBJ", "Subject")
            .add ("ISFT", "Software", tag::software)
            .add ("ISRC", "Source")
            .add ("ISRF", "Source Form")
            .add ("IMED", "Medium")
            .add ("ICMS", "Commissioned")
            .add ("IARL", "Archival Location")
            .add ("ILNG", "Language", tag::language)
            .add ("IWRI", "Written By", tag::lyricist)
            .add ("IMUS", "Composer", tag::composer)
            .add ("IPRO", "Producer")
            .add ("ISTR", "Starring")
            .add ("IDIT", "Digitization Date")
            .add ("ISHP", "Sharpness")
            .add ("ICRP", "Cropped")
            .add ("IDIM", "Dimensions")
            .add ("IDPI", "Dots per Inch")
            .add ("ILGT", "Lightness")
            .add ("IPLT", "Palette Setting");

        // Broadcast Wave bext chunk (WAV), EBU Tech 3285 version 2. Keys stay as PHI first named them.
        c.in (Schema::bext, "Broadcast Wave");
        const std::pair<const char*, const char*> bwfKeys[] {
            { tag::bwfDescription, "Description" }, { tag::bwfOriginator, "Originator" },
            { tag::bwfOriginatorRef, "Originator Reference" }, { tag::bwfDate, "Origination Date" },
            { tag::bwfTime, "Origination Time" }, { tag::bwfTimeReference, "Time Reference (samples)" },
            { tag::bwfUmid, "UMID" }, { tag::bwfLoudness, "Loudness Value (LUFS)" },
            { tag::bwfLoudnessRange, "Loudness Range (LU)" }, { tag::bwfTruePeak, "Max True Peak Level (dBTP)" },
            { tag::bwfMomentary, "Max Momentary Loudness (LUFS)" }, { tag::bwfShortTerm, "Max Short-Term Loudness (LUFS)" },
            { tag::bwfCodingHistory, "Coding History" },
        };
        const int bwfMax[] { 256, 32, 32, 10, 8, 0, 128, 0, 0, 0, 0, 0, 0 };
        for (size_t i = 0; i < std::size (bwfKeys); ++i)
        {
            c.add (std::string (bwfKeys[i].first).substr (4), bwfKeys[i].second, i == 0 ? tag::comment : "", F::same,
                   i == 0 || i + 1 == std::size (bwfKeys) ? K::lines : (i == 5 || (i >= 7 && i <= 11) ? K::number : K::text), bwfMax[i]);
            c.list.back().key = bwfKeys[i].first;
        }

        // Cart chunk (WAV), AES46: radio automation.
        c.in (Schema::cart, "Cart Chunk")
            .add ("Version", "Version", {}, F::same, K::text, 4)
            .add ("Title", "Title", tag::title, F::same, K::text, 64)
            .add ("Artist", "Artist", tag::artist, F::same, K::text, 64)
            .add ("CutID", "Cut ID", {}, F::same, K::text, 64)
            .add ("ClientID", "Client ID", {}, F::same, K::text, 64)
            .add ("Category", "Category", {}, F::same, K::text, 64)
            .add ("Classification", "Classification", {}, F::same, K::text, 64)
            .add ("OutCue", "Out Cue", {}, F::same, K::text, 64)
            .add ("StartDate", "Start Date (yyyy-mm-dd)", {}, F::same, K::text, 10)
            .add ("StartTime", "Start Time (hh:mm:ss)", {}, F::same, K::text, 8)
            .add ("EndDate", "End Date (yyyy-mm-dd)", {}, F::same, K::text, 10)
            .add ("EndTime", "End Time (hh:mm:ss)", {}, F::same, K::text, 8)
            .add ("ProducerAppID", "Producer App ID", {}, F::same, K::text, 64)
            .add ("ProducerAppVersion", "Producer App Version", {}, F::same, K::text, 64)
            .add ("UserDef", "User Defined", {}, F::same, K::text, 64)
            .add ("LevelReference", "Level Reference (0 dBFS in samples)", {}, F::same, K::number)
            .add ("URL", "URL", tag::url, F::same, K::text, 1024)
            .add ("TagText", "Tag Text", {}, F::same, K::lines);
        for (const auto* usage : { "SEG1", "SEG2", "SEG3", "INT1", "INT2", "INT3", "AUD1", "AUD2" })
            c.add (std::string ("Timer.") + usage, std::string ("Timer ") + usage + " (samples)", {}, F::same, K::number);
        c.add ("Timers", "Other Timers (USAGE=samples per line)", {}, F::same, K::lines);

        // iXML chunk (WAV): production sound metadata. Paths name the XML elements.
        c.in (Schema::ixml, "iXML")
            .add ("PROJECT", "Project")
            .add ("SCENE", "Scene")
            .add ("TAKE", "Take")
            .add ("TAPE", "Tape")
            .add ("CIRCLED", "Circled (TRUE/FALSE)")
            .add ("NO_GOOD", "No Good (TRUE/FALSE)")
            .add ("FALSE_START", "False Start (TRUE/FALSE)")
            .add ("WILD_TRACK", "Wild Track (TRUE/FALSE)")
            .add ("FILE_UID", "File UID")
            .add ("UBITS", "User Bits")
            .add ("NOTE", "Note", {}, F::same, K::lines)
            .add ("SPEED/NOTE", "Speed: Note")
            .add ("SPEED/MASTER_SPEED", "Speed: Master Speed")
            .add ("SPEED/CURRENT_SPEED", "Speed: Current Speed")
            .add ("SPEED/TIMECODE_RATE", "Speed: Timecode Rate")
            .add ("SPEED/TIMECODE_FLAG", "Speed: Timecode Flag (DF/NDF)")
            .add ("SPEED/FILE_SAMPLE_RATE", "Speed: File Sample Rate")
            .add ("SPEED/AUDIO_BIT_DEPTH", "Speed: Audio Bit Depth")
            .add ("SPEED/DIGITIZER_SAMPLE_RATE", "Speed: Digitizer Sample Rate")
            .add ("SPEED/TIMESTAMP_SAMPLES_SINCE_MIDNIGHT_HI", "Speed: Timestamp High")
            .add ("SPEED/TIMESTAMP_SAMPLES_SINCE_MIDNIGHT_LO", "Speed: Timestamp Low")
            .add ("SPEED/TIMESTAMP_SAMPLE_RATE", "Speed: Timestamp Sample Rate")
            .add ("SYNC_POINT_LIST/SYNC_POINT_COUNT", "Sync Point Count")
            .add ("HISTORY/ORIGINAL_FILENAME", "History: Original Filename")
            .add ("HISTORY/PARENT_FILENAME", "History: Parent Filename")
            .add ("HISTORY/PARENT_UID", "History: Parent UID")
            .add ("FILE_SET/TOTAL_FILES", "File Set: Total Files")
            .add ("FILE_SET/FAMILY_UID", "File Set: Family UID")
            .add ("FILE_SET/FAMILY_NAME", "File Set: Family Name")
            .add ("FILE_SET/FILE_SET_INDEX", "File Set: Index")
            .add ("TRACK_LIST/TRACK_COUNT", "Track Count")
            .add ("LOCATION/LOCATION_NAME", "Location: Name")
            .add ("LOCATION/LOCATION_GPS", "Location: GPS")
            .add ("LOCATION/LOCATION_ALTITUDE", "Location: Altitude")
            .add ("LOCATION/LOCATION_TYPE", "Location: Type")
            .add ("LOCATION/LOCATION_TIME", "Location: Time")
            .add ("USER", "User", {}, F::same, K::lines);
        for (int t = 1; t <= 8; ++t)
        {
            const auto base = "TRACK_LIST/TRACK[" + std::to_string (t) + "]/";
            c.add (base + "NAME", "Track " + std::to_string (t) + " Name");
            c.add (base + "FUNCTION", "Track " + std::to_string (t) + " Function");
        }

        // ID3v2 frames (MP3, WAV, AIFF). ID3v1 is written from the same values.
        c.in (Schema::id3, "ID3v2")
            .add ("TIT2", "Title", tag::title)
            .add ("TIT3", "Subtitle", tag::subtitle)
            .add ("TIT1", "Grouping", tag::grouping)
            .add ("TPE1", "Artist", tag::artist)
            .add ("TPE2", "Album Artist", tag::albumArtist)
            .add ("TPE3", "Conductor", tag::conductor)
            .add ("TPE4", "Remixed By")
            .add ("TALB", "Album", tag::album)
            .add ("TRCK", "Track", tag::track)
            .add ("TPOS", "Disc", tag::disc)
            .add ("TDRC", "Recording Date", tag::date)
            .add ("TDOR", "Original Release Date", tag::originalDate)
            .add ("TDRL", "Release Date")
            .add ("TCON", "Genre", tag::genre)
            .add ("COMM", "Comment", tag::comment, F::same, K::lines)
            .add ("TCOM", "Composer", tag::composer)
            .add ("TEXT", "Lyricist", tag::lyricist)
            .add ("USLT", "Lyrics", tag::lyrics, F::same, K::lines)
            .add ("TCOP", "Copyright", tag::copyright)
            .add ("TPUB", "Publisher", tag::publisher)
            .add ("TSRC", "ISRC", tag::isrc)
            .add ("TBPM", "BPM", tag::bpm)
            .add ("TKEY", "Initial Key", tag::key)
            .add ("TLAN", "Language", tag::language)
            .add ("TENC", "Encoded By", tag::encodedBy)
            .add ("TSSE", "Encoder Settings", tag::software)
            .add ("TMED", "Media Type")
            .add ("TOAL", "Original Album")
            .add ("TOPE", "Original Artist")
            .add ("TOLY", "Original Lyricist")
            .add ("TOWN", "File Owner")
            .add ("TRSN", "Internet Radio Station")
            .add ("TRSO", "Radio Station Owner")
            .add ("TSOA", "Album Sort Order")
            .add ("TSOP", "Performer Sort Order")
            .add ("TSOT", "Title Sort Order")
            .add ("TSST", "Set Subtitle")
            .add ("TMOO", "Mood")
            .add ("TPRO", "Produced Notice")
            .add ("TCMP", "Compilation (1)")
            .add ("TXXX:BARCODE", "Barcode (UPC/EAN)", tag::catalog)
            .add ("TXXX:CATALOGNUMBER", "Catalog Number")
            .add ("WOAR", "Artist Web Page", tag::url)
            .add ("WCOP", "Copyright Web Page")
            .add ("WOAF", "Audio File Web Page")
            .add ("WPUB", "Publisher Web Page")
            .add ("WOAS", "Audio Source Web Page")
            .add ("WCOM", "Commercial Web Page")
            .add ("WPAY", "Payment Web Page");

        // Vorbis comments (FLAC, Ogg, Opus).
        c.in (Schema::vorbis, "Vorbis Comments")
            .add ("TITLE", "Title", tag::title)
            .add ("VERSION", "Version", tag::subtitle)
            .add ("ARTIST", "Artist", tag::artist)
            .add ("ALBUMARTIST", "Album Artist", tag::albumArtist)
            .add ("ALBUM", "Album", tag::album)
            .add ("TRACKNUMBER", "Track Number", tag::track, F::number)
            .add ("TRACKTOTAL", "Track Total", tag::track, F::total)
            .add ("DISCNUMBER", "Disc Number", tag::disc, F::number)
            .add ("DISCTOTAL", "Disc Total", tag::disc, F::total)
            .add ("DATE", "Date", tag::date)
            .add ("ORIGINALDATE", "Original Date", tag::originalDate)
            .add ("GENRE", "Genre", tag::genre)
            .add ("COMMENT", "Comment", tag::comment, F::same, K::lines)
            .add ("DESCRIPTION", "Description", {}, F::same, K::lines)
            .add ("PERFORMER", "Performer")
            .add ("COMPOSER", "Composer", tag::composer)
            .add ("LYRICIST", "Lyricist", tag::lyricist)
            .add ("CONDUCTOR", "Conductor", tag::conductor)
            .add ("LYRICS", "Lyrics", tag::lyrics, F::same, K::lines)
            .add ("COPYRIGHT", "Copyright", tag::copyright)
            .add ("LICENSE", "License")
            .add ("ORGANIZATION", "Organization (Label)", tag::publisher)
            .add ("ISRC", "ISRC", tag::isrc)
            .add ("BARCODE", "Barcode (UPC/EAN)", tag::catalog)
            .add ("CATALOGNUMBER", "Catalog Number")
            .add ("BPM", "BPM", tag::bpm)
            .add ("INITIALKEY", "Initial Key", tag::key)
            .add ("LANGUAGE", "Language", tag::language)
            .add ("GROUPING", "Grouping", tag::grouping)
            .add ("LOCATION", "Location")
            .add ("CONTACT", "Contact", tag::url)
            .add ("ENCODEDBY", "Encoded By", tag::encodedBy)
            .add ("ENCODER", "Encoder", tag::software)
            .add ("MOOD", "Mood")
            .add ("REPLAYGAIN_TRACK_GAIN", "ReplayGain Track Gain")
            .add ("REPLAYGAIN_TRACK_PEAK", "ReplayGain Track Peak")
            .add ("REPLAYGAIN_ALBUM_GAIN", "ReplayGain Album Gain")
            .add ("REPLAYGAIN_ALBUM_PEAK", "ReplayGain Album Peak");

        // MP4 / iTunes atoms (M4A, AAC, ALAC).
        c.in (Schema::mp4, "MP4 / iTunes")
            .add ("\xC2\xA9nam", "Title", tag::title)
            .add ("\xC2\xA9" "ART", "Artist", tag::artist)
            .add ("aART", "Album Artist", tag::albumArtist)
            .add ("\xC2\xA9" "alb", "Album", tag::album)
            .add ("trkn", "Track", tag::track)
            .add ("disk", "Disc", tag::disc)
            .add ("\xC2\xA9" "day", "Year / Date", tag::date)
            .add ("\xC2\xA9" "gen", "Genre", tag::genre)
            .add ("\xC2\xA9" "cmt", "Comment", tag::comment, F::same, K::lines)
            .add ("\xC2\xA9" "wrt", "Composer", tag::composer)
            .add ("\xC2\xA9" "lyr", "Lyrics", tag::lyrics, F::same, K::lines)
            .add ("cprt", "Copyright", tag::copyright)
            .add ("tmpo", "BPM", tag::bpm)
            .add ("\xC2\xA9grp", "Grouping", tag::grouping)
            .add ("\xC2\xA9too", "Encoder", tag::software)
            .add ("\xC2\xA9" "enc", "Encoded By", tag::encodedBy)
            .add ("desc", "Description")
            .add ("cpil", "Compilation (1)")
            .add ("----:com.apple.iTunes:ISRC", "ISRC", tag::isrc)
            .add ("----:com.apple.iTunes:LABEL", "Label", tag::publisher)
            .add ("----:com.apple.iTunes:BARCODE", "Barcode (UPC/EAN)", tag::catalog)
            .add ("----:com.apple.iTunes:initialkey", "Initial Key", tag::key)
            .add ("----:com.apple.iTunes:LYRICIST", "Lyricist", tag::lyricist)
            .add ("----:com.apple.iTunes:CONDUCTOR", "Conductor", tag::conductor)
            .add ("----:com.apple.iTunes:SUBTITLE", "Subtitle", tag::subtitle)
            .add ("----:com.apple.iTunes:LANGUAGE", "Language", tag::language);

        // APEv2 (WavPack, Monkey's Audio, Musepack).
        c.in (Schema::ape, "APEv2")
            .add ("Title", "Title", tag::title)
            .add ("Subtitle", "Subtitle", tag::subtitle)
            .add ("Artist", "Artist", tag::artist)
            .add ("Album Artist", "Album Artist", tag::albumArtist)
            .add ("Album", "Album", tag::album)
            .add ("Track", "Track", tag::track)
            .add ("Disc", "Disc", tag::disc)
            .add ("Year", "Year / Date", tag::date)
            .add ("Genre", "Genre", tag::genre)
            .add ("Comment", "Comment", tag::comment, F::same, K::lines)
            .add ("Composer", "Composer", tag::composer)
            .add ("Lyricist", "Lyricist", tag::lyricist)
            .add ("Conductor", "Conductor", tag::conductor)
            .add ("Lyrics", "Lyrics", tag::lyrics, F::same, K::lines)
            .add ("Copyright", "Copyright", tag::copyright)
            .add ("Publisher", "Publisher", tag::publisher)
            .add ("ISRC", "ISRC", tag::isrc)
            .add ("Barcode", "Barcode (UPC/EAN)", tag::catalog)
            .add ("BPM", "BPM", tag::bpm)
            .add ("Language", "Language", tag::language)
            .add ("Related", "Related URL", tag::url)
            .add ("Media", "Media")
            .add ("Catalog", "Catalog Number");

        // XMP: the schemas Adobe's Metadata panels list, by namespace prefix.
        c.in (Schema::xmp, "Dublin Core")
            .xmp ("dc:title", "Title", X::langAlt, tag::title)
            .xmp ("dc:creator", "Creator", X::seq, tag::artist)
            .xmp ("dc:description", "Description", X::langAlt, tag::comment)
            .xmp ("dc:subject", "Subject (Keywords)", X::bag, tag::keywords)
            .xmp ("dc:rights", "Rights", X::langAlt, tag::copyright)
            .xmp ("dc:publisher", "Publisher", X::bag, tag::publisher)
            .xmp ("dc:contributor", "Contributor", X::bag)
            .xmp ("dc:date", "Date", X::seq)
            .xmp ("dc:language", "Language", X::bag, tag::language)
            .xmp ("dc:format", "Format")
            .xmp ("dc:identifier", "Identifier")
            .xmp ("dc:source", "Source")
            .xmp ("dc:relation", "Relation", X::bag)
            .xmp ("dc:coverage", "Coverage")
            .xmp ("dc:type", "Type", X::bag);
        c.in (Schema::xmp, "Basic")
            .xmp ("xmp:CreateDate", "Create Date")
            .xmp ("xmp:ModifyDate", "Modify Date")
            .xmp ("xmp:MetadataDate", "Metadata Date")
            .xmp ("xmp:CreatorTool", "Creator Tool", X::simple, tag::software)
            .xmp ("xmp:Label", "Label")
            .xmp ("xmp:Rating", "Rating (0 to 5)")
            .xmp ("xmp:Nickname", "Nickname")
            .xmp ("xmp:Identifier", "Identifier", X::bag)
            .xmp ("xmp:BaseURL", "Base URL")
            .xmp ("xmp:Advisory", "Advisory", X::bag);
        c.in (Schema::xmp, "Rights Management")
            .xmp ("xmpRights:Marked", "Copyright Status (True/False)")
            .xmp ("xmpRights:Owner", "Owner", X::bag)
            .xmp ("xmpRights:UsageTerms", "Usage Terms", X::langAlt)
            .xmp ("xmpRights:WebStatement", "Copyright Info URL")
            .xmp ("xmpRights:Certificate", "Certificate");
        c.in (Schema::xmp, "Media Management")
            .xmp ("xmpMM:DocumentID", "Document ID")
            .xmp ("xmpMM:InstanceID", "Instance ID")
            .xmp ("xmpMM:OriginalDocumentID", "Original Document ID")
            .xmp ("xmpMM:VersionID", "Version ID")
            .xmp ("xmpMM:RenditionClass", "Rendition Class")
            .xmp ("xmpMM:RenditionParams", "Rendition Parameters")
            .xmp ("xmpMM:Manager", "Manager")
            .xmp ("xmpMM:ManagerVariant", "Manager Variant")
            .xmp ("xmpMM:ManageTo", "Manage To")
            .xmp ("xmpMM:ManageUI", "Manage UI")
            .xmp ("xmpMM:PreservedFileName", "Preserved File Name");
        c.in (Schema::xmp, "Basic Job Ticket")
            .xmp ("xmpBJ:JobRef", "Job Reference", X::bag);
        c.in (Schema::xmp, "Paged-Text")
            .xmp ("xmpTPg:NPages", "Number of Pages")
            .xmp ("xmpTPg:PlateNames", "Plate Names", X::seq);
        c.in (Schema::xmp, "Dynamic Media")
            .xmp ("xmpDM:artist", "Artist", X::simple, tag::artist)
            .xmp ("xmpDM:album", "Album", X::simple, tag::album)
            .xmp ("xmpDM:trackNumber", "Track Number", X::simple, tag::track, F::number)
            .xmp ("xmpDM:discNumber", "Disc Number", X::simple, tag::disc)
            .xmp ("xmpDM:genre", "Genre", X::simple, tag::genre)
            .xmp ("xmpDM:composer", "Composer", X::simple, tag::composer)
            .xmp ("xmpDM:engineer", "Engineer", X::simple, tag::engineer)
            .xmp ("xmpDM:releaseDate", "Release Date", X::simple, tag::date)
            .xmp ("xmpDM:lyrics", "Lyrics", X::simple, tag::lyrics)
            .xmp ("xmpDM:tempo", "Tempo (BPM)", X::simple, tag::bpm)
            .xmp ("xmpDM:key", "Key (C, C#, D...)")
            .xmp ("xmpDM:scaleType", "Scale Type (Major, Minor, Both, Neither)")
            .xmp ("xmpDM:timeSignature", "Time Signature")
            .xmp ("xmpDM:numberOfBeats", "Number of Beats")
            .xmp ("xmpDM:instrument", "Instrument")
            .xmp ("xmpDM:loop", "Loop (True/False)")
            .xmp ("xmpDM:copyright", "Copyright (old)", X::simple, tag::copyright)
            .xmp ("xmpDM:logComment", "Log Comment")
            .xmp ("xmpDM:comment", "Comment")
            .xmp ("xmpDM:partOfCompilation", "Part of Compilation (True/False)")
            .xmp ("xmpDM:audioSampleRate", "Audio Sample Rate")
            .xmp ("xmpDM:audioSampleType", "Audio Sample Type")
            .xmp ("xmpDM:audioChannelType", "Audio Channel Type")
            .xmp ("xmpDM:audioCompressor", "Audio Compressor")
            .xmp ("xmpDM:speakerPlacement", "Speaker Placement")
            .xmp ("xmpDM:fileDataRate", "File Data Rate")
            .xmp ("xmpDM:introTime", "Intro Time")
            .xmp ("xmpDM:outCue", "Out Cue")
            .xmp ("xmpDM:projectName", "Project Name")
            .xmp ("xmpDM:scene", "Scene")
            .xmp ("xmpDM:shotName", "Shot Name")
            .xmp ("xmpDM:shotNumber", "Shot Number")
            .xmp ("xmpDM:shotDate", "Shot Date")
            .xmp ("xmpDM:shotDay", "Shot Day")
            .xmp ("xmpDM:shotLocation", "Shot Location")
            .xmp ("xmpDM:shotSize", "Shot Size")
            .xmp ("xmpDM:takeNumber", "Take Number")
            .xmp ("xmpDM:tapeName", "Tape Name")
            .xmp ("xmpDM:altTapeName", "Alternative Tape Name")
            .xmp ("xmpDM:good", "Good (True/False)")
            .xmp ("xmpDM:director", "Director")
            .xmp ("xmpDM:directorPhotography", "Director of Photography")
            .xmp ("xmpDM:client", "Client")
            .xmp ("xmpDM:cameraAngle", "Camera Angle")
            .xmp ("xmpDM:cameraLabel", "Camera Label")
            .xmp ("xmpDM:cameraModel", "Camera Model")
            .xmp ("xmpDM:cameraMove", "Camera Move")
            .xmp ("xmpDM:pullDown", "Pull Down")
            .xmp ("xmpDM:stretchMode", "Stretch Mode")
            .xmp ("xmpDM:videoFrameRate", "Video Frame Rate")
            .xmp ("xmpDM:videoCompressor", "Video Compressor")
            .xmp ("xmpDM:videoFieldOrder", "Video Field Order")
            .xmp ("xmpDM:videoPixelAspectRatio", "Video Pixel Aspect Ratio")
            .xmp ("xmpDM:absPeakAudioFilePath", "Absolute Peak Audio File Path")
            .xmp ("xmpDM:relativePeakAudioFilePath", "Relative Peak Audio File Path");
        c.in (Schema::xmp, "Adobe PDF")
            .xmp ("pdf:Keywords", "Keywords")
            .xmp ("pdf:Producer", "Producer")
            .xmp ("pdf:PDFVersion", "PDF Version")
            .xmp ("pdf:Trapped", "Trapped");
        c.in (Schema::xmp, "Photoshop")
            .xmp ("photoshop:Headline", "Headline")
            .xmp ("photoshop:Credit", "Credit")
            .xmp ("photoshop:Source", "Source")
            .xmp ("photoshop:Instructions", "Instructions")
            .xmp ("photoshop:AuthorsPosition", "Author's Position")
            .xmp ("photoshop:CaptionWriter", "Caption Writer")
            .xmp ("photoshop:Category", "Category")
            .xmp ("photoshop:SupplementalCategories", "Supplemental Categories", X::bag)
            .xmp ("photoshop:City", "City")
            .xmp ("photoshop:State", "State / Province")
            .xmp ("photoshop:Country", "Country")
            .xmp ("photoshop:DateCreated", "Date Created")
            .xmp ("photoshop:TransmissionReference", "Transmission Reference")
            .xmp ("photoshop:Urgency", "Urgency");
        c.in (Schema::xmp, "IPTC Core")
            .xmp ("Iptc4xmpCore:CountryCode", "Country Code")
            .xmp ("Iptc4xmpCore:Location", "Sublocation")
            .xmp ("Iptc4xmpCore:IntellectualGenre", "Intellectual Genre")
            .xmp ("Iptc4xmpCore:Scene", "Scene Code", X::bag)
            .xmp ("Iptc4xmpCore:SubjectCode", "Subject Code", X::bag);
        c.in (Schema::xmp, "IPTC Extension")
            .xmp ("Iptc4xmpExt:PersonInImage", "Person Shown", X::bag)
            .xmp ("Iptc4xmpExt:Event", "Event", X::langAlt)
            .xmp ("Iptc4xmpExt:OrganisationInImageName", "Organisation Name", X::bag)
            .xmp ("Iptc4xmpExt:AddlModelInfo", "Additional Model Information")
            .xmp ("Iptc4xmpExt:DigitalSourceType", "Digital Source Type");
        c.in (Schema::xmp, "Camera Raw")
            .xmp ("crs:RawFileName", "Raw File Name")
            .xmp ("crs:Version", "Version");
        c.in (Schema::xmp, "EXIF: TIFF Properties")
            .xmp ("tiff:Make", "Make")
            .xmp ("tiff:Model", "Model")
            .xmp ("tiff:Software", "Software")
            .xmp ("tiff:Artist", "Artist")
            .xmp ("tiff:Copyright", "Copyright", X::langAlt)
            .xmp ("tiff:DateTime", "Date Time")
            .xmp ("tiff:ImageDescription", "Image Description", X::langAlt);
        c.in (Schema::xmp, "EXIF Properties")
            .xmp ("exif:DateTimeOriginal", "Date Time Original")
            .xmp ("exif:DateTimeDigitized", "Date Time Digitized")
            .xmp ("exif:UserComment", "User Comment", X::langAlt)
            .xmp ("exif:GPSLatitude", "GPS Latitude")
            .xmp ("exif:GPSLongitude", "GPS Longitude")
            .xmp ("exif:GPSAltitude", "GPS Altitude");
        c.in (Schema::xmp, "Additional EXIF Properties")
            .xmp ("aux:SerialNumber", "Serial Number")
            .xmp ("aux:Lens", "Lens")
            .xmp ("aux:Owner", "Owner");
        c.in (Schema::xmp, "Script")
            .xmp ("xmpScript:action", "Action")
            .xmp ("xmpScript:character", "Character", X::bag)
            .xmp ("xmpScript:dialog", "Dialog")
            .xmp ("xmpScript:sceneSetting", "Scene Setting")
            .xmp ("xmpScript:sceneCharacteristics", "Scene Characteristics");

        return c;
    }

    const Catalog& catalog()
    {
        static const Catalog c = build();
        return c;
    }

    bool startsWith (const std::string& s, std::string_view prefix) { return s.compare (0, prefix.size(), prefix) == 0; }

    std::string trim (std::string s)
    {
        const auto notSpace = [] (unsigned char ch) { return ! std::isspace (ch); };
        s.erase (s.begin(), std::find_if (s.begin(), s.end(), notSpace));
        s.erase (std::find_if (s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    std::string tagOr (const Tags& tags, const std::string& key)
    {
        const auto it = tags.find (key);
        return it == tags.end() ? std::string() : it->second;
    }

    // What a field follows from its common key.
    std::string followed (const Tags& tags, const Field& f)
    {
        if (f.common.empty())
            return {};
        const auto value = tagOr (tags, f.common);
        switch (f.follow)
        {
            case Follow::number: return numberPart (value);
            case Follow::total:  return totalPart (value);
            case Follow::year:   return value.substr (0, std::min<size_t> (4, value.size()));
            case Follow::same:   break;
        }
        return value;
    }

    const std::vector<Schema> order { Schema::riff, Schema::bext, Schema::cart, Schema::ixml, Schema::id3,
                                      Schema::vorbis, Schema::mp4, Schema::ape, Schema::xmp };
}

const std::vector<Field>& fields() { return catalog().list; }

std::vector<const Field*> fieldsOf (Schema s)
{
    std::vector<const Field*> out;
    for (const auto& f : fields())
        if (f.schema == s)
            out.push_back (&f);
    return out;
}

const Field* find (const std::string& key)
{
    static const auto byKey = []
    {
        std::map<std::string, const Field*> m;
        for (const auto& f : fields())
            m[f.key] = &f;
        return m;
    }();
    const auto it = byKey.find (key);
    return it == byKey.end() ? nullptr : it->second;
}

const Field* findId (Schema s, const std::string& id)
{
    for (const auto& f : fields())
        if (f.schema == s && f.id == id)
            return &f;
    return nullptr;
}

const std::vector<std::pair<std::string, std::string>>& commonKeys()
{
    static const std::vector<std::pair<std::string, std::string>> keys {
        { tag::title, "Title" }, { tag::artist, "Artist" }, { tag::album, "Album" }, { tag::albumArtist, "Album Artist" },
        { tag::track, "Track (3 or 3/12)" }, { tag::disc, "Disc (1 or 1/2)" }, { tag::date, "Date" }, { tag::genre, "Genre" },
        { tag::comment, "Comment" }, { tag::composer, "Composer" }, { tag::lyricist, "Lyricist" }, { tag::conductor, "Conductor" },
        { tag::subtitle, "Subtitle" }, { tag::grouping, "Grouping" }, { tag::copyright, "Copyright" }, { tag::publisher, "Publisher / Label" },
        { tag::isrc, "ISRC" }, { tag::catalog, "UPC/EAN" }, { tag::bpm, "BPM" }, { tag::key, "Key" }, { tag::language, "Language" },
        { tag::keywords, "Keywords" }, { tag::lyrics, "Lyrics" }, { tag::url, "Web Page" }, { tag::engineer, "Engineer" },
        { tag::encodedBy, "Encoded By" }, { tag::software, "Software" }, { tag::originalDate, "Original Release Date" },
    };
    return keys;
}

const char* schemaName (Schema s)
{
    switch (s)
    {
        case Schema::common: return "Common";
        case Schema::riff:   return "RIFF";
        case Schema::bext:   return "BWF";
        case Schema::cart:   return "Cart";
        case Schema::ixml:   return "iXML";
        case Schema::id3:    return "ID3";
        case Schema::vorbis: return "Vorbis";
        case Schema::mp4:    return "MP4";
        case Schema::ape:    return "APE";
        case Schema::xmp:    return "XMP";
    }
    return "";
}

const char* schemaPrefix (Schema s)
{
    switch (s)
    {
        case Schema::common: return "";
        case Schema::riff:   return "riff";
        case Schema::bext:   return "bwf";
        case Schema::cart:   return "cart";
        case Schema::ixml:   return "ixml";
        case Schema::id3:    return "id3";
        case Schema::vorbis: return "vorbis";
        case Schema::mp4:    return "mp4";
        case Schema::ape:    return "ape";
        case Schema::xmp:    return "xmp";
    }
    return "";
}

std::vector<Schema> schemas() { return order; }

std::string keyFor (Schema s, const std::string& id)
{
    if (s == Schema::common)
        return id;
    if (s == Schema::bext)
        return std::string ("bwf.") + id;
    return std::string (schemaPrefix (s)) + ":" + id;
}

Schema schemaOfKey (const std::string& key)
{
    if (startsWith (key, "bwf."))
        return Schema::bext;
    if (startsWith (key, "xmpraw:") || startsWith (key, "xmpns:"))
        return Schema::xmp;
    const auto colon = key.find (':');
    if (colon == std::string::npos)
        return Schema::common;
    const auto prefix = key.substr (0, colon);
    for (const auto s : order)
        if (prefix == schemaPrefix (s))
            return s;
    return Schema::common;
}

std::string get (const Tags& tags, const Field& f)
{
    if (const auto it = tags.find (f.key); it != tags.end())
        return it->second;
    return followed (tags, f);
}

void set (Tags& tags, const Field& f, const std::string& value)
{
    if (! f.common.empty() && value == followed (tags, f))
        tags.erase (f.key);
    else if (value.empty() && f.common.empty())
        tags.erase (f.key);
    else
        tags[f.key] = value;
}

std::vector<std::pair<std::string, std::string>> valuesFor (const Tags& tags, Schema s)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::set<std::string> known;
    for (const auto* f : fieldsOf (s))
    {
        known.insert (f->key);
        if (auto v = get (tags, *f); ! v.empty())
            out.emplace_back (f->id, std::move (v));
    }

    if (s == Schema::common)
        return out;

    const auto prefix = s == Schema::bext ? std::string ("bwf.") : std::string (schemaPrefix (s)) + ":";
    for (auto it = tags.lower_bound (prefix); it != tags.end() && startsWith (it->first, prefix); ++it)
        if (known.count (it->first) == 0 && ! it->second.empty())
            out.emplace_back (it->first.substr (prefix.size()), it->second);
    return out;
}

void absorb (Tags& tags, Schema s, const std::vector<std::pair<std::string, std::string>>& values)
{
    // Numbers before totals, so "3" and "12" become "3/12".
    auto sorted = values;
    std::stable_sort (sorted.begin(), sorted.end(), [s] (const auto& a, const auto& b)
    {
        const auto* fa = findId (s, a.first);
        const auto* fb = findId (s, b.first);
        return (fa != nullptr && fa->follow == Follow::total ? 1 : 0) < (fb != nullptr && fb->follow == Follow::total ? 1 : 0);
    });

    for (const auto& [id, raw] : sorted)
    {
        const auto value = s == Schema::xmp || s == Schema::ixml ? raw : trim (raw);
        if (value.empty())
            continue;
        const auto* f = findId (s, id);
        if (f == nullptr)
        {
            tags[keyFor (s, id)] = value;
            continue;
        }
        if (f->common.empty())
        {
            tags[f->key] = value;
            continue;
        }

        auto& common = tags[f->common];
        bool merged = false;
        switch (f->follow)
        {
            case Follow::same:
            case Follow::year:
                if (common.empty())
                    common = value;
                merged = followed (tags, *f) == value;
                break;
            case Follow::number:
                if (common.empty())
                    common = value;
                else if (numberPart (common).empty())
                    common = value + common;   // "/12" -> "3/12"
                merged = numberPart (common) == value;
                break;
            case Follow::total:
                if (! common.empty() && totalPart (common).empty() && common.find ('/') == std::string::npos)
                    common += "/" + value;
                else if (common.empty())
                    common = "/" + value;
                merged = totalPart (common) == value;
                break;
        }
        if (merged)
            tags.erase (f->key);
        else
            tags[f->key] = value;
    }

    for (auto it = tags.begin(); it != tags.end();)
        it = it->second.empty() ? tags.erase (it) : std::next (it);
}

std::vector<Conflict> conflicts (const Tags& tags)
{
    std::vector<Conflict> out;
    for (const auto& [key, value] : tags)
        if (const auto* f = find (key); f != nullptr && ! f->common.empty())
            if (const auto expected = followed (tags, *f); expected != value)
                out.push_back ({ f->common, key, expected, value });
    return out;
}

void sync (Tags& tags)
{
    for (const auto& c : conflicts (tags))
        tags.erase (c.key);
}

void clearSchema (Tags& tags, Schema s)
{
    for (auto it = tags.begin(); it != tags.end();)
        it = s != Schema::common && schemaOfKey (it->first) == s ? tags.erase (it) : std::next (it);
}

//==============================================================================
std::string numberPart (const std::string& s)
{
    return trim (s.substr (0, s.find ('/')));
}

std::string totalPart (const std::string& s)
{
    const auto slash = s.find ('/');
    return slash == std::string::npos ? std::string() : trim (s.substr (slash + 1));
}

//==============================================================================
namespace
{
    const char* base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode (const std::string& in)
{
    std::string out;
    out.reserve ((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3)
    {
        const auto n = ((unsigned) (unsigned char) in[i] << 16) | ((unsigned) (unsigned char) in[i + 1] << 8) | (unsigned char) in[i + 2];
        for (int k = 3; k >= 0; --k)
            out += base64Chars[(n >> (6 * k)) & 63];
    }
    if (i < in.size())
    {
        auto n = (unsigned) (unsigned char) in[i] << 16;
        if (i + 1 < in.size())
            n |= (unsigned) (unsigned char) in[i + 1] << 8;
        out += base64Chars[(n >> 18) & 63];
        out += base64Chars[(n >> 12) & 63];
        out += i + 1 < in.size() ? base64Chars[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string base64Decode (const std::string& in)
{
    std::string out;
    unsigned value = 0;
    int bits = 0;
    for (const auto ch : in)
    {
        const auto* p = std::strchr (base64Chars, ch);
        if (ch == '=' || ch == 0 || p == nullptr)
            continue;
        value = (value << 6) | (unsigned) (p - base64Chars);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out += (char) ((value >> bits) & 0xff);
        }
    }
    return out;
}

//==============================================================================
// "picture.N": "type;mime;description" on the first line, then the image as base64.
std::vector<Picture> pictures (const Tags& tags)
{
    std::vector<Picture> out;
    for (auto it = tags.lower_bound ("picture."); it != tags.end() && startsWith (it->first, "picture."); ++it)
    {
        const auto& v = it->second;
        const auto nl = v.find ('\n');
        if (nl == std::string::npos)
            continue;
        const auto head = v.substr (0, nl);
        const auto a = head.find (';');
        const auto b = a == std::string::npos ? a : head.find (';', a + 1);
        Picture p;
        p.type = std::atoi (head.substr (0, a).c_str());
        if (b != std::string::npos)
        {
            p.mime = head.substr (a + 1, b - a - 1);
            p.description = head.substr (b + 1);
        }
        p.data = base64Decode (v.substr (nl + 1));
        if (! p.data.empty())
            out.push_back (std::move (p));
    }
    return out;
}

void setPictures (Tags& tags, const std::vector<Picture>& list)
{
    for (auto it = tags.lower_bound ("picture."); it != tags.end() && startsWith (it->first, "picture.");)
        it = tags.erase (it);
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& p = list[i];
        auto description = p.description;
        std::replace (description.begin(), description.end(), '\n', ' ');
        tags["picture." + std::to_string (i)] = std::to_string (p.type) + ";" + (p.mime.empty() ? mimeOfImage (p.data) : p.mime) + ";"
                                                + description + "\n" + base64Encode (p.data);
    }
}

const char* pictureTypeName (int type)
{
    static const char* names[] {
        "Other", "File Icon", "Other File Icon", "Front Cover", "Back Cover", "Leaflet Page", "Media (CD)", "Lead Artist",
        "Artist", "Conductor", "Band", "Composer", "Lyricist", "Recording Location", "During Recording", "During Performance",
        "Video Screen Capture", "A Bright Coloured Fish", "Illustration", "Band Logo", "Publisher Logo",
    };
    return type >= 0 && type < (int) std::size (names) ? names[type] : "Other";
}

std::string mimeOfImage (const std::string& b)
{
    if (b.size() > 3 && (unsigned char) b[0] == 0xff && (unsigned char) b[1] == 0xd8)
        return "image/jpeg";
    if (b.size() > 8 && b.compare (1, 3, "PNG") == 0)
        return "image/png";
    if (b.size() > 6 && b.compare (0, 3, "GIF") == 0)
        return "image/gif";
    if (b.size() > 12 && b.compare (0, 4, "RIFF") == 0 && b.compare (8, 4, "WEBP") == 0)
        return "image/webp";
    return {};
}

//==============================================================================
std::string expand (const std::string& pattern, const Tags& tags, const std::vector<std::pair<std::string, std::string>>& vars)
{
    std::string out;
    for (size_t i = 0; i < pattern.size(); ++i)
    {
        const auto close = pattern[i] == '{' ? pattern.find ('}', i) : std::string::npos;
        if (close == std::string::npos)
        {
            out += pattern[i];
            continue;
        }
        const auto name = pattern.substr (i + 1, close - i - 1);
        bool found = false;
        for (const auto& [k, v] : vars)
            if (k == name)
            {
                out += v;
                found = true;
                break;
            }
        if (! found)
        {
            bool common = false;
            for (const auto& [k, label] : commonKeys())
                common = common || k == name;
            if (common || tags.count (name) > 0)
                out += tagOr (tags, name);
            else
                out += pattern.substr (i, close - i + 1);
        }
        i = close;
    }
    return out;
}

//==============================================================================
namespace
{
    std::string csvField (const std::string& s)
    {
        if (s.find_first_of (",\"\n\r") == std::string::npos)
            return s;
        std::string out = "\"";
        for (const auto ch : s)
            out += ch == '"' ? std::string ("\"\"") : std::string (1, ch);
        return out + "\"";
    }

    bool exportable (const std::string& key) { return ! startsWith (key, "picture.") && ! startsWith (key, "raw:") && ! startsWith (key, "xmpraw:"); }
}

std::string toCsv (const std::vector<Row>& rows)
{
    std::vector<std::string> columns;
    std::set<std::string> seen;
    for (const auto& [key, label] : commonKeys())
        columns.push_back (key), seen.insert (key);
    for (const auto& r : rows)
        for (const auto& [key, value] : r.tags)
            if (exportable (key) && seen.insert (key).second)
                columns.push_back (key);

    std::string out = "file";
    for (const auto& c : columns)
        out += "," + csvField (c);
    out += "\n";
    for (const auto& r : rows)
    {
        out += csvField (r.file);
        for (const auto& c : columns)
            out += "," + csvField (tagOr (r.tags, c));
        out += "\n";
    }
    return out;
}

std::vector<Row> fromCsv (const std::string& text, std::string& error)
{
    std::vector<std::vector<std::string>> table (1);
    std::string field;
    bool quoted = false, any = false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const auto ch = text[i];
        if (quoted)
        {
            if (ch == '"' && i + 1 < text.size() && text[i + 1] == '"')
                field += '"', ++i;
            else if (ch == '"')
                quoted = false;
            else
                field += ch;
            continue;
        }
        if (ch == '"')
            quoted = true, any = true;
        else if (ch == ',')
            table.back().push_back (field), field.clear(), any = true;
        else if (ch == '\n' || ch == '\r')
        {
            if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            if (any || ! field.empty())
                table.back().push_back (field);
            field.clear();
            any = false;
            table.emplace_back();
        }
        else
            field += ch, any = true;
    }
    if (any || ! field.empty())
        table.back().push_back (field);
    while (! table.empty() && table.back().empty())
        table.pop_back();

    if (table.empty() || table[0].empty() || table[0][0] != "file")
    {
        error = "The first row must name the columns, starting with \"file\".";
        return {};
    }
    std::vector<Row> rows;
    for (size_t r = 1; r < table.size(); ++r)
    {
        Row row;
        for (size_t c = 0; c < table[r].size() && c < table[0].size(); ++c)
            if (c == 0)
                row.file = table[r][c];
            else if (! table[r][c].empty())
                row.tags[table[0][c]] = table[r][c];
        rows.push_back (std::move (row));
    }
    return rows;
}

namespace
{
    std::string jsonString (const std::string& s)
    {
        std::string out = "\"";
        for (const auto ch : s)
        {
            switch (ch)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if ((unsigned char) ch < 0x20)
                    {
                        char buf[8];
                        std::snprintf (buf, sizeof buf, "\\u%04x", (unsigned) ch);
                        out += buf;
                    }
                    else
                        out += ch;
            }
        }
        return out + "\"";
    }

    struct JsonReader
    {
        const std::string& s;
        size_t i = 0;
        std::string error;

        void space() { while (i < s.size() && std::isspace ((unsigned char) s[i])) ++i; }
        bool eat (char ch) { space(); if (i < s.size() && s[i] == ch) { ++i; return true; } return false; }
        bool expect (char ch) { if (eat (ch)) return true; error = std::string ("Expected '") + ch + "' at character " + std::to_string (i); return false; }

        static void utf8 (std::string& out, unsigned cp)
        {
            if (cp < 0x80) out += (char) cp;
            else if (cp < 0x800) { out += (char) (0xc0 | (cp >> 6)); out += (char) (0x80 | (cp & 63)); }
            else if (cp < 0x10000) { out += (char) (0xe0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 63)); out += (char) (0x80 | (cp & 63)); }
            else { out += (char) (0xf0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 63)); out += (char) (0x80 | ((cp >> 6) & 63)); out += (char) (0x80 | (cp & 63)); }
        }

        bool string (std::string& out)
        {
            if (! expect ('"'))
                return false;
            while (i < s.size() && s[i] != '"')
            {
                if (s[i] != '\\')
                {
                    out += s[i++];
                    continue;
                }
                if (++i >= s.size())
                    break;
                const auto e = s[i++];
                switch (e)
                {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u':
                    {
                        auto cp = (unsigned) std::strtoul (s.substr (i, 4).c_str(), nullptr, 16);
                        i += 4;
                        if (cp >= 0xd800 && cp < 0xdc00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u')
                        {
                            const auto low = (unsigned) std::strtoul (s.substr (i + 2, 4).c_str(), nullptr, 16);
                            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                            i += 6;
                        }
                        utf8 (out, cp);
                        break;
                    }
                    default: out += e;
                }
            }
            return expect ('"');
        }

        // A string, number, true/false or null, as text.
        bool scalar (std::string& out)
        {
            space();
            if (i < s.size() && s[i] == '"')
                return string (out);
            const auto start = i;
            while (i < s.size() && (std::isalnum ((unsigned char) s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.'))
                ++i;
            out = s.substr (start, i - start);
            if (out == "null")
                out.clear();
            if (start == i)
                error = "Expected a value at character " + std::to_string (i);
            return start != i;
        }
    };
}

std::string toJson (const std::vector<Row>& rows)
{
    std::string out = "[\n";
    for (size_t r = 0; r < rows.size(); ++r)
    {
        out += "  { \"file\": " + jsonString (rows[r].file) + ", \"tags\": {";
        bool first = true;
        for (const auto& [key, value] : rows[r].tags)
            if (exportable (key))
            {
                out += std::string (first ? "\n" : ",\n") + "      " + jsonString (key) + ": " + jsonString (value);
                first = false;
            }
        out += first ? "} }" : "\n    } }";
        out += r + 1 < rows.size() ? ",\n" : "\n";
    }
    return out + "]\n";
}

std::vector<Row> fromJson (const std::string& text, std::string& error)
{
    JsonReader j { text };
    std::vector<Row> rows;
    if (! j.expect ('['))
        return error = j.error, rows;
    if (j.eat (']'))
        return rows;
    do
    {
        Row row;
        if (! j.expect ('{'))
            return error = j.error, std::vector<Row>();
        if (! j.eat ('}'))
        {
            do
            {
                std::string name;
                if (! j.string (name) || ! j.expect (':'))
                    return error = j.error, std::vector<Row>();
                if (name == "tags")
                {
                    if (! j.expect ('{'))
                        return error = j.error, std::vector<Row>();
                    if (! j.eat ('}'))
                    {
                        do
                        {
                            std::string key, value;
                            if (! j.string (key) || ! j.expect (':') || ! j.scalar (value))
                                return error = j.error, std::vector<Row>();
                            if (! value.empty())
                                row.tags[key] = value;
                        } while (j.eat (','));
                        if (! j.expect ('}'))
                            return error = j.error, std::vector<Row>();
                    }
                }
                else
                {
                    std::string value;
                    if (! j.scalar (value))
                        return error = j.error, std::vector<Row>();
                    if (name == "file")
                        row.file = value;
                }
            } while (j.eat (','));
            if (! j.expect ('}'))
                return error = j.error, std::vector<Row>();
        }
        rows.push_back (std::move (row));
    } while (j.eat (','));
    if (! j.expect (']'))
        return error = j.error, std::vector<Row>();
    return rows;
}
}
