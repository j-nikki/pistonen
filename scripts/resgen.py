import argparse
import gzip
import os
import sys
from contextlib import suppress

ap = argparse.ArgumentParser()
ap.add_argument('debug', type=int)
ap.add_argument('dst')
ap.add_argument('ifs', nargs='+')
args = ap.parse_args()

minify = False
if not args.debug:
    with suppress(ImportError):
        import minify_html
        minify = True

def namecnv(name):
    return name.lstrip('res').rstrip('index.html')
def get_file(name):
    with open(name, encoding='U8') as f:
        return namecnv(name), f.read()
names, contents = zip(*sorted(map(get_file, args.ifs)))

hdr = """#include <string_view>
#include <array>

namespace res {{
constexpr inline std::array<std::string_view, {0}> names{{{1}}};
extern constinit const std::array<std::string_view, {0}> contents;
}}"""
src = """#include "res.h"

const std::array<std::string_view, {}> res::contents{{{}}};
"""

with open(os.path.join(args.dst, 'res.h'), 'w') as f:
    f.write(hdr.format(len(names),
                       ','.join(f'"{name}"' for name in names)))

def contcnv(cont):
    if minify:
        cont = minify_html.minify(cont, minify_css=True, minify_js=True)
    cont = gzip.compress(cont.encode('U8'))
    return 'std::string_view{"' + ''.join(f'\\{x:o}' for x in cont) + f'",{len(cont)}}}'
with open(os.path.join(args.dst, 'res.cpp'), 'w') as f:
    f.write(src.format(len(names),
                       ','.join(map(contcnv, contents))))
