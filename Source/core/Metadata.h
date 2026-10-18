// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioDocument.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Every metadata schema PHINYX reads and writes, like Audition's Metadata panel.
// Plain C++.
//
// A document's Tags hold two kinds of keys:
// - common keys ("title", "artist"... see `tag`), which every schema follows;
// - schema keys ("riff:INAM", "id3:TIT2", "xmp:dc:title"...), set only where a
//   schema says something else than the common key (a conflict), or holds a
//   field no other schema has.
// So a title typed once is written as INAM, TIT2, TITLE, ©nam and dc:title,
// while a file whose ID3 title differs from its RIFF title keeps both.
//
// Other keys: "picture.N" (cover art, see Picture), "xmpns:<prefix>" (an XMP
// namespace URI), "xmpraw:<prefix>:<name>" (an XMP property kept as its XML),
// "raw:wav:<id>" (a WAV chunk PHINYX does not know, kept as base64).
namespace meta
{
    enum class Schema { common, riff, bext, cart, ixml, id3, vorbis, mp4, ape, xmp };

    // How a schema field follows its common key.
    enum class Follow { same, number, total, year };   // "3/12" -> "3", "12"; "2026-10-18" -> "2026"

    enum class Kind { text, lines, number };   // lines: several lines, or one item per line (XMP lists)

    // XMP value types (for the RDF written).
    enum class XmpType { simple, langAlt, bag, seq };

    struct Field
    {
        Schema schema;
        std::string key;        // Tags key
        std::string id;         // INAM, TIT2, dc:title...
        std::string label;
        std::string group;      // XMP: the schema's name ("Dublin Core"); others: a section
        std::string common;     // common key followed, or empty
        Follow follow = Follow::same;
        Kind kind = Kind::text;
        XmpType xmpType = XmpType::simple;
        int maxLength = 0;      // fixed-size fields (bext, cart): bytes
    };

    const std::vector<Field>& fields();
    std::vector<const Field*> fieldsOf (Schema);
    const Field* find (const std::string& key);
    const Field* findId (Schema, const std::string& id);
    // The common keys, in display order, with their labels.
    const std::vector<std::pair<std::string, std::string>>& commonKeys();

    const char* schemaName (Schema);         // "RIFF", "ID3"...
    const char* schemaPrefix (Schema);       // "riff", "id3"... (Tags key prefix)
    std::vector<Schema> schemas();           // every schema but common, in display order
    // The schema a Tags key belongs to (common for plain keys).
    Schema schemaOfKey (const std::string& key);
    std::string keyFor (Schema, const std::string& id);

    // The value a schema field has: its own key, or what it follows.
    std::string get (const Tags&, const Field&);
    // Sets a schema field: kept as its own key only where it differs from what it follows.
    void set (Tags&, const Field&, const std::string& value);

    // Values to write for a schema: every field with a value (by id), plus
    // schema keys PHINYX has no field for (kept from the file).
    std::vector<std::pair<std::string, std::string>> valuesFor (const Tags&, Schema);

    // Adds what a file's schema holds to `tags`: fills common keys that are
    // empty and keeps differing values as schema keys. Call once per schema
    // read, most trusted first.
    void absorb (Tags&, Schema, const std::vector<std::pair<std::string, std::string>>& values);

    struct Conflict
    {
        std::string common, key, commonValue, value;
    };
    std::vector<Conflict> conflicts (const Tags&);
    // Makes every schema follow the common keys again (removes conflicting schema keys).
    void sync (Tags&);
    // Removes every key of a schema.
    void clearSchema (Tags&, Schema);

    // Cover art and other pictures (ID3 APIC, FLAC/Vorbis PICTURE, MP4 covr, APE cover).
    struct Picture
    {
        int type = 3;              // ID3/FLAC picture type: 3 front cover, 4 back cover...
        std::string mime;          // image/jpeg, image/png
        std::string description;
        std::string data;          // the image file's bytes
    };
    std::vector<Picture> pictures (const Tags&);
    void setPictures (Tags&, const std::vector<Picture>&);
    const char* pictureTypeName (int type);
    std::string mimeOfImage (const std::string& bytes);   // from the first bytes; empty when unknown

    std::string base64Encode (const std::string& bytes);
    std::string base64Decode (const std::string& text);

    // Template text with {variables}: {title}, {artist}, any common key, plus
    // what `vars` adds ({filename}, {date}, {lufs}...). Unknown names stay as written.
    std::string expand (const std::string& pattern, const Tags&, const std::vector<std::pair<std::string, std::string>>& vars = {});

    // Spreadsheet exchange: one row per file, a column per key.
    struct Row
    {
        std::string file;
        Tags tags;
    };
    std::string toCsv (const std::vector<Row>&);
    std::vector<Row> fromCsv (const std::string&, std::string& error);
    std::string toJson (const std::vector<Row>&);
    std::vector<Row> fromJson (const std::string&, std::string& error);

    // Number and total of "3/12".
    std::string numberPart (const std::string&);
    std::string totalPart (const std::string&);
}
