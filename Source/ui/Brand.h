// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// How the app's name is shown where Unicode is safe (window titles, About):
// the Greek letter phi, then the plain name. APP_DISPLAY_NAME stays ASCII for
// paths, tags, plugin hosts and anything else that may not take UTF-8.
#define APP_BRAND_NAME "Φ " APP_DISPLAY_NAME

// The Φ logo, built into the program (packaging/phi.svg).
#define APP_ICON ":/packaging/phi.svg"
