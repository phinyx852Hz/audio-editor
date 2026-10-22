# SPDX-License-Identifier: AGPL-3.0-only
#
# Cuts Cardinal down to its foundational modules before it is built: Cardinal's
# own host modules and VCV Fundamental (see cardinal-plugins.cpp). Only those
# two module folders are checked out; this makes Cardinal's makefiles build,
# and bundle the resources of, only them. Run in Cardinal's source folder:
#   python3 cardinal_foundational.py <cardinal-plugins.cpp>

import re
import shutil
import sys

shutil.copyfile(sys.argv[1], "plugins/plugins.cpp")

MARK = "# PHINYX: foundational modules only (cmake/instruments/cardinal_foundational.py)\n"
with open("plugins/Makefile") as f:
    lines = f.readlines()
if lines and lines[0] == MARK:
    sys.exit(0)

# Source, binary and resource lists name every module collection: keep the lines
# of the two kept collections (and the lists' plain wildcards), less the
# Cardinal modules that are left out.
assignment = re.compile(r"^(?:MINI)?(?:PLUGIN_FILES|PLUGIN_BINARIES|RESOURCE_FILES)\s*[+:]?=\s*(.*)$|^JACK_RESOURCES\s*[+:]?=\s*(.*)$")
kept = re.compile(r"(^|[ (,])(Cardinal|Fundamental)/")
dropped = re.compile(r"AIDA-X|AudioFile|AudioToCVPitch|Carla|Ildaeil|SassyScope|TextEditor|ImGui|glBars|Miku")

out = [MARK]
for line in lines:
    m = assignment.match(line.rstrip("\n"))
    if m:
        value = m.group(1) if m.group(1) is not None else m.group(2)
        plain = "/" not in value or value == "\\" or value.startswith("$(wildcard */") or value.startswith("$(wildcard $(addsuffix")
        if not plain and (not kept.search(value) or dropped.search(value)):
            line = "# " + line
    # Wildcards over every collection's folder (some have files in Cardinal's own tree) look in the two only.
    line = re.sub(r"\$\(wildcard \*/([^)]*)\)", r"$(wildcard $(addsuffix /\1,Cardinal Fundamental))", line)
    if line.startswith("PLUGIN_LIST = "):
        line = "PLUGIN_LIST = Cardinal Fundamental\n"
    # The plugins-mini libraries (for Cardinal Mini) are not needed.
    if line.startswith("TARGETS = plugins$(TARGET_SUFFIX).a plugins-mini"):
        line = "TARGETS = plugins$(TARGET_SUFFIX).a\n"
    elif line == "\t$(MAKE) HEADLESS=true plugins-mini-headless.a\n":
        line = "\t@true\n"
    out.append(line)

with open("plugins/Makefile", "w") as f:
    f.writelines(out)

# Surge XT's libraries are built and linked only for its modules.
with open("deps/Makefile") as f:
    deps = f.read()
with open("deps/Makefile", "w") as f:
    f.write(deps.replace("TARGETS += $(SURGE_LIB)\n", ""))
with open("src/Makefile.cardinal.mk") as f:
    lines = f.readlines()
with open("src/Makefile.cardinal.mk", "w") as f:
    f.writelines("# " + l if l.startswith("RACK_EXTRA_LIBS += $(SURGE_DEP_PATH)") else l for l in lines)
