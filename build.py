import os
from subprocess import check_call

def read_or(path, def_=''):
    try:
        with open(path) as f:
            return f.read()
    except BaseException:
        return def_

cmakeFiles = [
    "CMakeLists.txt",
    "src/CMakeLists.txt",
    "CMakePresets.json",
    "CMakeUserPresets.json",
    ".cmake-args"
]
ts = ' '.join(str(os.stat(x).st_mtime_ns) for x in cmakeFiles if os.path.exists(x))

if not os.path.exists('out/build/CMakeCache.txt') or read_or('.ts') != ts:
    check_call(['cmake', '-Wno-dev', '-S', '.', '-B', 'out/build',
               '--preset', read_or(".cpreset", "debug")])
    with open('.ts', 'w') as f:
        f.write(ts)

check_call(['cmake', '--build', '--preset', read_or(".bpreset", "test")])
