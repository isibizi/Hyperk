# Writes data/daylight.html from include/daylight_page.h.
#
# Everything under data/ is embedded by web_embedder.py and served by the Hyperk
# web server on port 80, so the settings page becomes reachable as
# http://<device>/daylight.html without naming a port. The page keeps its own
# copy on port 8080; the header stays the single place to edit it.
#
# Must run before web_embedder.py.

import os
import re

Import("env")

project_dir = env.get("PROJECT_DIR")
source = os.path.join(project_dir, "include", "daylight_page.h")
target = os.path.join(project_dir, "data", "daylight.html")

version = "0.0.0"
for flag in env.get("CPPDEFINES", []):
    if isinstance(flag, tuple) and flag[0] == "APP_VERSION":
        version = str(flag[1]).replace('\\"', "").strip('"')
        break

build = "dev"
for flag in env.get("CPPDEFINES", []):
    if isinstance(flag, tuple) and flag[0] == "HYPERK_DAYLIGHT_BUILD":
        build = str(flag[1]).replace('\\"', "").strip('"')
        break

if not os.path.exists(source):
    print("[DaylightPage] %s not found, nothing to export" % source)
else:
    text = open(source, encoding="utf-8").read()

    # The page is one or more raw string literals glued together with macros.
    # Reassemble it the way the compiler would.
    pieces = re.findall(r'R"HTML\((.*?)\)HTML"', text, flags=re.DOTALL)
    if not pieces:
        print("[DaylightPage] no page found in the header")
    else:
        page = pieces[0]
        for glue, piece in zip(
            re.findall(r'\)HTML"(.*?)R"HTML\(', text, flags=re.DOTALL), pieces[1:]
        ):
            for token in re.findall(r'APP_VERSION|HYPERK_DAYLIGHT_BUILD|"([^"]*)"', glue):
                pass
            resolved = ""
            for token in re.findall(r'APP_VERSION|HYPERK_DAYLIGHT_BUILD|"[^"]*"', glue):
                if token == "APP_VERSION":
                    resolved += version
                elif token == "HYPERK_DAYLIGHT_BUILD":
                    resolved += build
                else:
                    resolved += token[1:-1]
            page += resolved + piece

        os.makedirs(os.path.dirname(target), exist_ok=True)
        previous = open(target, encoding="utf-8").read() if os.path.exists(target) else None
        if previous != page:
            open(target, "w", encoding="utf-8").write(page)
        print("[DaylightPage] data/daylight.html written, %d bytes" % len(page))
