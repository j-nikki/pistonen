import os
from subprocess import call


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
    ".cmake-args",
    ".bpreset",
    ".cpreset",
]
ts = ' '.join(str(os.stat(x).st_mtime_ns)
              for x in cmakeFiles if os.path.exists(x))

if not os.path.exists('build/CMakeCache.txt') or read_or('.ts') != ts:
    ret = call(['cmake', '-Wno-dev', '-S', '.', '-B', 'build',
               '--preset', read_or(".cpreset", "debug").splitlines()[0]])
    if ret != 0:
        exit(ret)
    with open('.ts', 'w') as f:
        f.write(ts)
    exit(1)

exit(call(['cmake', '--build', '--preset', read_or(".bpreset", "test")]))
