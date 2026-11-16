#!/bin/bash
# Make a fresh git worktree build incrementally by reusing the objects of /home/user/cdda-dev.
# Usage: setup_worktree.sh <path to your worktree>   (run once, before your first make)
set -e
W=$(cd "$1" && pwd); cd "$W"
git ls-files src tests | xargs touch -d "2026-11-16 06:00:00"
mkdir -p obj pch
cp -a /home/user/cdda-dev/obj/tiles obj/
cp -a /home/user/cdda-dev/pch/*.gch pch/ 2>/dev/null || true
touch -d "2026-11-16 06:00:00" pch/main-pch.hpp; find obj/tiles/pch -name "*.gch" -exec touch -d "2026-11-16 06:30:00" {} + 2>/dev/null || true
cp -a /home/user/cdda-dev/src/version.h src/ 2>/dev/null || true
cp -a /home/user/cdda-dev/data/shaders/*.spv /home/user/cdda-dev/data/shaders/*.stamp data/shaders/ 2>/dev/null || true
echo "ready"
