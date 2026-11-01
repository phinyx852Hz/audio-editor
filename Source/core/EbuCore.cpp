// SPDX-License-Identifier: AGPL-3.0-only
#include "EbuCore.h"

#include "Metadata.h"
#include "MiniXml.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>

namespace ebucore
{
namespace
{
    const char* const ebucoreUri = "urn:ebu:metadata-schema:ebucore";
    const char* const dcUri = "http://purl.org/dc/elements/1.1/";

    using Node = minixml::Node;
    using Values = std::map<std::string, std::string>;   // field id -> value (lines joined by \n)

    const Node* childLocal (const Node& n, const std::string& local)
    {
        for (const auto& c : n.children)
            if (c.localName() == local)
                return &c;
        return nullptr;
    }

    std::string trimmed (const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace ((unsigned char) s[a]))
            ++a;
        while (b > a && std::isspace ((unsigned char) s[b - 1]))
            --b;
        return s.substr (a, b - a);
    }

    std::string childText (const Node& n, const std::string& local)
    {
        const auto* c = childLocal (n, local);
        return c != nullptr ? trimmed (c->text) : std::string();
    }

    std::string attr (const Node& n, const std::string& name)
    {
        const auto* a = n.attribute (name);
        return a != nullptr ? *a : std::string();
    }

    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return s;
    }

    // A person's or organisation's name in an entityType (creator, contributor, publisher).
    std::string entityName (const Node& n)
    {
        if (const auto* contact = childLocal (n, "contactDetails"))
        {
            if (auto name = childText (*contact, "name"); ! name.empty())
                return name;
            auto given = childText (*contact, "givenName"), family = childText (*contact, "familyName");
            if (! given.empty() || ! family.empty())
                return trimmed (given + " " + family);
        }
        if (const auto* org = childLocal (n, "organisationDetails"))
            return childText (*org, "organisationName");
        return {};
    }

    std::set<std::string> roles (const Node& n)
    {
        std::set<std::string> out;
        for (const auto& c : n.children)
            if (c.localName() == "role")
                out.insert (lower (trimmed (attr (c, "typeLabel").empty() ? c.text : attr (c, "typeLabel"))));
        return out;
    }

    std::vector<std::string> lines (const std::string& s)
    {
        std::vector<std::string> out;
        size_t start = 0;
        while (start <= s.size())
        {
            const auto nl = s.find ('\n', start);
            if (auto line = trimmed (s.substr (start, nl == std::string::npos ? std::string::npos : nl - start)); ! line.empty())
                out.push_back (std::move (line));
            if (nl == std::string::npos)
                break;
            start = nl + 1;
        }
        return out;
    }

    std::string join (const std::vector<std::string>& v)
    {
        std::string out;
        for (const auto& s : v)
            out += (out.empty() ? "" : "\n") + s;
        return out;
    }

    // Element names for writing: the document's own prefixes.
    struct Names
    {
        std::string ebu, dc;
        std::string e (const std::string& local) const { return ebu.empty() ? local : ebu + ":" + local; }
        std::string d (const std::string& local) const { return dc + ":" + local; }
    };

    std::string esc (const std::string& s) { return minixml::escape (s); }

    // A group of coreMetadata elements PHI reads and writes as a whole.
    struct Group
    {
        std::string element;                                   // local name under coreMetadata
        std::vector<std::string> fields;                       // field ids it holds
        std::function<bool (const Node&)> matches;
        std::function<void (const std::vector<const Node*>&, Values&)> extract;
        std::function<std::string (const Names&, const Values&)> render;   // the elements, one per line
    };

    std::string entity (const Names& n, const std::string& element, const std::string& name, const std::string& role, bool organisation)
    {
        std::string out = "<" + n.e (element) + ">";
        if (organisation)
            out += "<" + n.e ("organisationDetails") + "><" + n.e ("organisationName") + ">" + esc (name) + "</" + n.e ("organisationName")
                 + "></" + n.e ("organisationDetails") + ">";
        else
            out += "<" + n.e ("contactDetails") + "><" + n.e ("name") + ">" + esc (name) + "</" + n.e ("name") + "></" + n.e ("contactDetails") + ">";
        if (! role.empty())
            out += "<" + n.e ("role") + " typeLabel=\"" + minixml::escape (role, true) + "\"/>";
        return out + "</" + n.e (element) + ">\n";
    }

    // Creators or contributors with one role (or, with role empty, those with none of `others`).
    Group people (const std::string& element, const std::string& field, const std::string& role, std::set<std::string> others)
    {
        Group g;
        g.element = element;
        g.fields = { field };
        g.matches = [role, others] (const Node& n)
        {
            const auto r = roles (n);
            if (! role.empty())
                return r.count (lower (role)) > 0;
            return std::none_of (others.begin(), others.end(), [&] (const auto& o) { return r.count (o) > 0; });
        };
        g.extract = [field] (const std::vector<const Node*>& nodes, Values& v)
        {
            std::vector<std::string> names;
            for (const auto* n : nodes)
                if (auto name = entityName (*n); ! name.empty())
                    names.push_back (std::move (name));
            if (! names.empty())
                v[field] = join (names);
        };
        g.render = [element, field, role] (const Names& n, const Values& v)
        {
            std::string out;
            if (const auto it = v.find (field); it != v.end())
                for (const auto& name : lines (it->second))
                    out += entity (n, element, name, role, false);
            return out;
        };
        return g;
    }

    // An element holding dc:<local> texts: title, subject, description, rights, language.
    Group dcText (const std::string& element, const std::string& dcLocal, const std::string& field, bool many)
    {
        Group g;
        g.element = element;
        g.fields = { field };
        g.matches = [] (const Node&) { return true; };
        g.extract = [=] (const std::vector<const Node*>& nodes, Values& v)
        {
            std::vector<std::string> found;
            for (const auto* n : nodes)
                for (const auto& c : n->children)
                    if (c.localName() == dcLocal && c.prefix() != "" && ! trimmed (c.text).empty())
                        found.push_back (many ? trimmed (c.text) : c.text);
            if (! found.empty())
                v[field] = many ? join (found) : found.front();
        };
        g.render = [=] (const Names& n, const Values& v)
        {
            std::string out;
            const auto it = v.find (field);
            if (it == v.end() || trimmed (it->second).empty())
                return out;
            const auto items = many ? lines (it->second) : std::vector<std::string> { it->second };
            for (const auto& item : items)
                out += "<" + n.e (element) + "><" + n.d (dcLocal) + ">" + esc (item) + "</" + n.d (dcLocal) + "></" + n.e (element) + ">\n";
            return out;
        };
        return g;
    }

    std::string dateAttributes (const std::string& value)
    {
        const auto v = trimmed (value);
        const auto digits = [&] (size_t from, size_t count)
        {
            return v.size() >= from + count && std::all_of (v.begin() + (long) from, v.begin() + (long) (from + count), [] (unsigned char c) { return std::isdigit (c); });
        };
        if (digits (0, 4) && v.size() >= 10 && v[4] == '-' && digits (5, 2) && v[7] == '-' && digits (8, 2))
            return " startDate=\"" + v.substr (0, 10) + "\"";
        if (digits (0, 4) && (v.size() == 4 || ! std::isdigit ((unsigned char) v[4])))
            return " startYear=\"" + v.substr (0, 4) + "\"";
        return {};
    }

    std::string dateOf (const Node& n)
    {
        for (const auto* a : { "startDate", "date", "startYear", "year" })
            if (auto v = attr (n, a); ! v.empty())
                return v;
        return trimmed (n.text);
    }

    const std::vector<Group>& groups()
    {
        static const std::vector<Group> all = []
        {
            std::vector<Group> g;
            g.push_back (dcText ("title", "title", "title", false));
            g.push_back (people ("creator", "composer", "Composer", {}));
            g.push_back (people ("creator", "lyricist", "Lyricist", {}));
            g.push_back (people ("creator", "creator", {}, { "composer", "lyricist" }));
            g.push_back (people ("contributor", "conductor", "Conductor", {}));
            g.push_back (people ("contributor", "engineer", "Engineer", {}));
            g.push_back (people ("contributor", "contributor", {}, { "conductor", "engineer" }));

            Group publisher;
            publisher.element = "publisher";
            publisher.fields = { "publisher" };
            publisher.matches = [] (const Node&) { return true; };
            publisher.extract = [] (const std::vector<const Node*>& nodes, Values& v)
            {
                for (const auto* n : nodes)
                    if (auto name = entityName (*n); ! name.empty())
                    {
                        v["publisher"] = name;
                        return;
                    }
            };
            publisher.render = [] (const Names& n, const Values& v)
            {
                const auto it = v.find ("publisher");
                return it == v.end() || trimmed (it->second).empty() ? std::string() : entity (n, "publisher", trimmed (it->second), {}, true);
            };
            g.push_back (publisher);

            g.push_back (dcText ("subject", "subject", "subject", true));
            g.push_back (dcText ("description", "description", "description", false));

            Group date;
            date.element = "date";
            date.fields = { "created", "issued" };
            date.matches = [] (const Node&) { return true; };
            date.extract = [] (const std::vector<const Node*>& nodes, Values& v)
            {
                for (const auto* n : nodes)
                    for (const auto& c : n->children)
                    {
                        const auto local = c.localName();
                        if ((local == "created" || local == "issued") && v.count (local) == 0)
                            if (auto d = dateOf (c); ! d.empty())
                                v[local] = d;
                        if (local == "date" && v.count ("created") == 0 && ! trimmed (c.text).empty())
                            v["created"] = trimmed (c.text);
                    }
            };
            date.render = [] (const Names& n, const Values& v)
            {
                std::string inner;
                for (const auto* which : { "created", "issued" })
                    if (const auto it = v.find (which); it != v.end() && ! trimmed (it->second).empty())
                    {
                        if (const auto a = dateAttributes (it->second); ! a.empty())
                            inner += "<" + n.e (which) + a + "/>";
                        else if (std::string (which) == "created")
                            inner += "<" + n.d ("date") + ">" + esc (trimmed (it->second)) + "</" + n.d ("date") + ">";
                    }
                return inner.empty() ? std::string() : "<" + n.e ("date") + ">" + inner + "</" + n.e ("date") + ">\n";
            };
            g.push_back (date);

            Group type;
            type.element = "type";
            type.fields = { "genre" };
            type.matches = [] (const Node& n) { return childLocal (n, "genre") != nullptr; };
            type.extract = [] (const std::vector<const Node*>& nodes, Values& v)
            {
                std::vector<std::string> genres;
                for (const auto* n : nodes)
                    for (const auto& c : n->children)
                        if (c.localName() == "genre")
                            if (auto label = trimmed (attr (c, "typeLabel").empty() ? c.text : attr (c, "typeLabel")); ! label.empty())
                                genres.push_back (label);
                if (! genres.empty())
                    v["genre"] = join (genres);
            };
            type.render = [] (const Names& n, const Values& v)
            {
                std::string inner;
                if (const auto it = v.find ("genre"); it != v.end())
                    for (const auto& genre : lines (it->second))
                        inner += "<" + n.e ("genre") + " typeLabel=\"" + minixml::escape (genre, true) + "\"/>";
                return inner.empty() ? std::string() : "<" + n.e ("type") + ">" + inner + "</" + n.e ("type") + ">\n";
            };
            g.push_back (type);

            Group isrc;
            isrc.element = "identifier";
            isrc.fields = { "isrc" };
            isrc.matches = [] (const Node& n) { return lower (attr (n, "typeLabel")) == "isrc" || lower (attr (n, "formatLabel")) == "isrc"; };
            isrc.extract = [] (const std::vector<const Node*>& nodes, Values& v)
            {
                for (const auto* n : nodes)
                    if (auto id = childText (*n, "identifier"); ! id.empty())
                    {
                        v["isrc"] = id;
                        return;
                    }
            };
            isrc.render = [] (const Names& n, const Values& v)
            {
                const auto it = v.find ("isrc");
                if (it == v.end() || trimmed (it->second).empty())
                    return std::string();
                return "<" + n.e ("identifier") + " typeLabel=\"ISRC\" formatLabel=\"ISRC\"><" + n.d ("identifier") + ">" + esc (trimmed (it->second))
                     + "</" + n.d ("identifier") + "></" + n.e ("identifier") + ">\n";
            };
            g.push_back (isrc);

            g.push_back (dcText ("rights", "rights", "rights", false));
            g.push_back (dcText ("language", "language", "language", true));
            return g;
        }();
        return all;
    }

    // The group an element of coreMetadata belongs to, or -1.
    int groupOf (const Node& n)
    {
        const auto local = n.localName();
        const auto& all = groups();
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].element == local && all[i].matches (n))
                return (int) i;
        return -1;
    }

    std::vector<std::vector<const Node*>> byGroup (const Node& core)
    {
        std::vector<std::vector<const Node*>> out (groups().size());
        for (const auto& c : core.children)
            if (const auto g = groupOf (c); g >= 0)
                out[(size_t) g].push_back (&c);
        return out;
    }

    Values extractAll (const Node& core)
    {
        Values v;
        const auto nodes = byGroup (core);
        for (size_t i = 0; i < groups().size(); ++i)
            groups()[i].extract (nodes[i], v);
        return v;
    }

    bool parseEbuCore (const std::string& xml, Node& root, const Node*& core, std::string& error)
    {
        core = nullptr;
        if (! minixml::parse (xml, root, error))
            return false;
        if (root.localName() != "ebuCoreMain")
        {
            error = "Not EBUCore (no ebuCoreMain element)";
            return false;
        }
        core = childLocal (root, "coreMetadata");
        return true;
    }
}

bool read (const std::string& xml, Tags& tags, std::string& error)
{
    Node root;
    const Node* core = nullptr;
    if (! parseEbuCore (xml, root, core, error))
        return false;
    if (core == nullptr)
        return true;
    const auto values = extractAll (*core);
    meta::absorb (tags, meta::Schema::ebucore, { values.begin(), values.end() });
    return true;
}

std::string write (const Tags& tags, const std::string& original)
{
    Values now;
    for (const auto* f : meta::fieldsOf (meta::Schema::ebucore))
        if (auto v = meta::get (tags, *f); ! trimmed (v).empty())
            now[f->id] = v;

    Node root;
    const Node* core = nullptr;
    std::string error;
    const bool merging = ! original.empty();
    if (merging && ! parseEbuCore (original, root, core, error))
        return {};   // an axml chunk that is not EBUCore is kept as it was
    if (! merging && now.empty())
        return {};

    Names names;
    names.ebu = core != nullptr ? core->prefix() : (merging ? root.prefix() : std::string());
    std::string dcDeclared;
    if (merging)
        for (const auto& [k, v] : root.attributes)
            if (v == dcUri && k.rfind ("xmlns:", 0) == 0)
                dcDeclared = k.substr (6);
    names.dc = dcDeclared.empty() ? "dc" : dcDeclared;

    const auto& all = groups();
    const auto was = core != nullptr ? extractAll (*core) : Values();
    std::vector<bool> changed (all.size(), false);
    for (size_t i = 0; i < all.size(); ++i)
        for (const auto& f : all[i].fields)
        {
            const auto a = was.find (f);
            const auto b = now.find (f);
            const auto before = a == was.end() ? std::string() : a->second;
            const auto after = b == now.end() ? std::string() : b->second;
            changed[i] = changed[i] || before != after;
        }

    const auto verbatim = [&] (const Node& n) { return original.substr (n.begin, n.end - n.begin) + "\n"; };

    std::string body;
    std::vector<bool> written (all.size(), false);
    if (core != nullptr)
        for (const auto& c : core->children)
        {
            const auto g = groupOf (c);
            if (g < 0 || ! changed[(size_t) g])
                body += verbatim (c);
            else if (! written[(size_t) g])
            {
                body += all[(size_t) g].render (names, now);
                written[(size_t) g] = true;
            }
        }
    for (size_t i = 0; i < all.size(); ++i)
        if (changed[i] && ! written[i])
            body += all[i].render (names, now);

    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    if (merging)
    {
        out += "<" + root.name;
        for (const auto& [k, v] : root.attributes)
            out += " " + k + "=\"" + minixml::escape (v, true) + "\"";
        if (dcDeclared.empty())
            out += std::string (" xmlns:dc=\"") + dcUri + "\"";
        out += ">\n";
        bool coreDone = false;
        for (const auto& c : root.children)
        {
            if (&c == core)
            {
                out += "<" + c.name;
                for (const auto& [k, v] : c.attributes)
                    out += " " + k + "=\"" + minixml::escape (v, true) + "\"";
                out += ">\n" + body + "</" + c.name + ">\n";
                coreDone = true;
            }
            else
                out += verbatim (c);
        }
        if (! coreDone)
            out = out + "<" + names.e ("coreMetadata") + ">\n" + body + "</" + names.e ("coreMetadata") + ">\n";
        out += "</" + root.name + ">\n";
    }
    else
    {
        out += std::string ("<ebuCoreMain xmlns=\"") + ebucoreUri + "\" xmlns:dc=\"" + dcUri + "\" version=\"1.10\">\n";
        out += "<coreMetadata>\n" + body + "</coreMetadata>\n</ebuCoreMain>\n";
    }
    return out;
}
}
