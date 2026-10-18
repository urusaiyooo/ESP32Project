"""Redact local build paths from __FILE__ and debug metadata in public firmware."""
Import('env')
import os
from pathlib import Path

paths={str(Path.home()),str(Path.home()).replace('\\','/'),env.subst('$PROJECT_DIR')}
variants=set()
for path in paths:
    variants.update((path,path.lower(),path.upper(),path.replace('\\','/'),path.replace('/','\\')))
env.Append(CCFLAGS=[f'-ffile-prefix-map={path}=/build' for path in sorted(variants)])
