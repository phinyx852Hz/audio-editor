// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// phi --analyze-plugin <name or id> [options]: measures a plugin as the Plugin
// Analyzer does and prints the report, Forensic Φ included. Returns the exit code.
inline constexpr const char* analyzeOption = "--analyze-plugin";
int analyzePluginCommand (int argc, char* argv[]);
