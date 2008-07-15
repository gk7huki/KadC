from build_config import *

lib_env = Environment()
lib_env.Append(CPPFLAGS = flgs)
lib_env.Append(CPPPATH=include_search_path)
lib_env.Append(CPPDEFINES=defs)

lib_env.Library('KadC', Glob('src/*.c'))
