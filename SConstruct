# Build system copied/modified from
# http://www.scons.org/cgi-bin/wiki/AdvancedBuildExample
# Original by zedshaw@zedshaw.com
import os
import sys
from build_support import *
from build_config import *

opts = Variables('custom.py')
opts.Add(EnumVariable('win32', 'Build for win32 target', 'no',
                    allowed_values=('yes','no')))
env = Environment(variables = opts)
Help(opts.GenerateHelpText(env))

platform = None
if (env.get('win32') == 'yes'):
    env.Tool('mingw')
    env['CC'] = 'i686-w64-mingw32-gcc'
    env['CXX'] = 'i686-w64-mingw32-g++'
    env['AR'] = 'i686-w64-mingw32-ar'
    env['RANLIB'] = 'i686-w64-mingw32-ranlib'
    env['BUILD_PLATFORM'] = 'win32'
    platform = 'win32'
else:
    env.Tool('gcc')
    env['BUILD_PLATFORM'] = sys.platform

env.Append(CPPFLAGS = cflags)
env.Append(LINKFLAGS = lflags)
env.Append(CPPPATH = include_search_path)
env.Append(CPPDEFINES = defs)

# Construct target directories and names.
target_dir = '#' + SelectBuildDir(build_base_dir, platform)
target_name = os.sep.join([target_dir, target_name])

## Get the sources
sources_raw = DirGlob(source_base_dir, '*.c')
## Now must make a pure version relative to the build directory
sources = []
for source in sources_raw:
    sources.append(os.sep.join([target_dir] + source.split(os.sep)[1:]))

env.VariantDir(target_dir, source_base_dir, duplicate=0)
env.Library(target_name, sources)
