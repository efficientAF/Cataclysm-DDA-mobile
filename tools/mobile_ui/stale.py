#!/usr/bin/env python3
"""Touch the sources whose object files are older than a header they include.

The Makefile's dependency files are close to empty when the precompiled header
is in use (gcc leaves out what it reads after one), so `make` does not rebuild
a .cpp after a header it includes has changed.  Two object files then disagree
about a struct, and the game crashes in odd places.  Run this from the
repository before `make`; tools/mobile_ui/build.sh does.
"""
import os
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def includes(path, cache={}):
    if path not in cache:
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                names = INCLUDE.findall(f.read())
        except OSError:
            names = []
        found = []
        for name in names:
            for base in (os.path.dirname(path), "src"):
                candidate = os.path.normpath(os.path.join(base, name))
                if os.path.isfile(candidate):
                    found.append(candidate)
                    break
        cache[path] = found
    return cache[path]


def newest_header(source):
    seen = set()
    stack = list(includes(source))
    newest = 0.0
    while stack:
        header = stack.pop()
        if header in seen:
            continue
        seen.add(header)
        newest = max(newest, os.path.getmtime(header))
        stack.extend(includes(header))
    return newest


def main():
    touched = 0
    for src_dir, obj_dirs in (("src", ("obj/tiles", "obj")), ("tests", ("tests/obj/tiles", "tests/obj"))):
        if not os.path.isdir(src_dir):
            continue
        for name in sorted(os.listdir(src_dir)):
            if not name.endswith(".cpp"):
                continue
            source = os.path.join(src_dir, name)
            objects = [os.path.join(d, name[:-4] + ".o") for d in obj_dirs]
            built = [os.path.getmtime(o) for o in objects if os.path.isfile(o)]
            if not built:
                continue
            if newest_header(source) > min(built):
                os.utime(source)
                touched += 1
                print("stale:", source)
    print(f"{touched} source file(s) touched", file=sys.stderr)


if __name__ == "__main__":
    main()
