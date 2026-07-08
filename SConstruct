import re
import os
import subprocess

exp = Split('env')
sconscript = [
    'GUI',
    'Utility/util',
    'Console',
    'Goop',
    'Net',
    'OmfgScript',
    'liero2gus',
    'luaapi',
    'lighter',
    'http',
]

def is_List(e):
    return isinstance(e, list)

def getBinName(env, bin_name):
    conf = env.get('MY_CONF', 'posix')
    if conf == 'mingw-cross':
        bin_name += '.exe'
    return os.path.join('#bin', conf, bin_name)

def getLibName(env, lib):
    return os.path.join('#lib', env.get('MY_SUBFOLDER', ''), lib)

def getObjects(env, directory='.'):
    if is_List(directory):
        l = []
        for d in directory:
            l += env.getObjects(d)
        return l
        
    sourcePattern = re.compile(r'\.(cpp|c)$')
    subfolder = env.get('MY_SUBFOLDER', '')
    buildDir = os.path.join(directory, '.build', subfolder)
    env.VariantDir(buildDir, directory, duplicate=0)
    
    return [env.Object(os.path.join(buildDir, i))
            for i in os.listdir(directory)
            if sourcePattern.search(i)]

# Initialize Environment
env = Environment(ENV=os.environ.copy())

# Add custom methods
env.AddMethod(getBinName)
env.AddMethod(getLibName)
env.AddMethod(getObjects)

# Generate compile_commands.json for IDE/LSP support
env.Tool('compilation_db')

# Set custom variables
env['MY_CONF'] = ARGUMENTS.get('conf', 'posix')
env['MY_BUILD'] = ARGUMENTS.get('build', 'release')
env['MY_SUBFOLDER'] = os.path.join(env['MY_CONF'], env['MY_BUILD'])
env['NO_PARSERS'] = ARGUMENTS.get('no-parsers', False)
env['CCCOMSTR'] = 'Compiling $TARGET'
env['CXXCOMSTR'] = 'Compiling $TARGET'
env['LINKCOMSTR'] = 'Linking $TARGET'

# Sanitizer support: sanitize=address,undefined or sanitize=thread, etc.
# Debug builds default to address,undefined; pass sanitize=none to disable.
_sanitize_arg = ARGUMENTS.get('sanitize', '')
if _sanitize_arg == '':
    _sanitize_list = []
else:
    _sanitize_list = [s.strip() for s in _sanitize_arg.split(',') if s.strip()]

if _sanitize_list:
    _san_flags = ' '.join(f'-fsanitize={s}' for s in _sanitize_list)
    env.Append(CCFLAGS=Split(_san_flags),
               LINKFLAGS=Split(_san_flags))


# Add Homebrew paths for Linux
brew_prefix = '/home/linuxbrew/.linuxbrew'
if os.path.exists(brew_prefix):
    env.Append(CPPPATH=[os.path.join(brew_prefix, 'include')])
    env.Append(LIBPATH=[os.path.join(brew_prefix, 'lib')])
    pkg_config_path = os.path.join(brew_prefix, 'lib', 'pkgconfig')
    
    if 'PKG_CONFIG_PATH' in env['ENV']:
        env['ENV']['PKG_CONFIG_PATH'] += os.pathsep + pkg_config_path
    else:
        env['ENV']['PKG_CONFIG_PATH'] = pkg_config_path
    
    brew_bin = os.path.join(brew_prefix, 'bin')
    if brew_bin not in env['ENV']['PATH']:
        env['ENV']['PATH'] = brew_bin + os.pathsep + env['ENV']['PATH']

env.Append(
    CPPPATH=Split('. #http #luaapi #Console #GUI #Utility #OmfgScript #Goop #Net'),
    LIBPATH=[os.path.join('#lib', env['MY_SUBFOLDER']), os.path.join('#lib', env['MY_CONF'])],
    CCFLAGS=Split('-pipe -fno-diagnostics-show-caret -fno-diagnostics-show-option -Wfatal-errors -Wall -Wno-unused -Wno-register -Wno-implicit-fallthrough'),
    CXXFLAGS=Split('-std=c++17'),
    CPPDEFINES=['_GNU_SOURCE', 'BOOST_TIMER_ENABLE_DEPRECATED']
)

if env['MY_BUILD'] == 'release':
    env.Append(CCFLAGS=Split('-O3 -g'),
               CPPDEFINES=['NDEBUG'])
elif env['MY_BUILD'] == 'debug':
    env.Append(CCFLAGS=Split('-Og -g -fno-omit-frame-pointer -Wextra'),
               CPPDEFINES=['DEBUG', 'MAP_DOWNLOADING', 'LOG_RUNTIME'])
elif env['MY_BUILD'] == 'dedserv':
    env.Append(CCFLAGS=Split('-O3 -g'),
               CPPDEFINES=['NDEBUG', 'DEDSERV'])
elif env['MY_BUILD'] == 'dedserv-debug':
    env.Append(CCFLAGS=Split('-Og -g -fno-omit-frame-pointer'),
               CPPDEFINES=['DEBUG', 'DEDSERV', 'LOG_RUNTIME'])

# Dependency Detection
libs = ['sdl3', 'sdl3-mixer', 'sdl3-image', 'sdl3-ttf', 'libenet', 'libpng', 'zlib']
for lib in libs:
    try:
        env.ParseConfig(f'pkg-config --cflags --libs {lib}')
    except Exception as e:
        print(f"Warning: Could not find {lib} via pkg-config. Error: {e}")

# Boost Detection
# In Boost 1.70+ boost_system was merged into boost_filesystem, so only
# link it separately for older versions.
boost_libs = ['boost_filesystem']
for blib in boost_libs:
    if not env.GetOption('clean'):
        # LIBPATH is already set via pkg-config and Homebrew paths above.
        # Just verify the library exists by trying a simple compile+link.
        conf = Configure(env)
        # Temporarily extend LIBPATH for the check
        orig_libpath = list(env.get('LIBPATH', []))
        env.Append(LIBPATH=['/usr/lib/x86_64-linux-gnu', os.path.join(brew_prefix, 'lib')])
        if conf.CheckLib(blib, language='C++'):
            # Force shared linking to avoid mixed-ABI issues with Boost 1.70+
            # where v4 path algorithms are in the shared library only.
            env.Append(LINKLIBS=blib)
            # Ensure dynamic linking for boost (shared lib takes precedence)
            env.AppendUnique(LINKFLAGS=['-Wl,-Bdynamic'])
        else:
            print(f"Warning: Could not find boost library {blib}")
        env = conf.Finish()
        env['LIBPATH'] = orig_libpath

# LuaJIT Detection
try:
    env.ParseConfig('pkg-config --cflags --libs luajit')
    print("Found LuaJIT via pkg-config")
except Exception as e:
    print(f"Warning: Could not find LuaJIT via pkg-config. Error: {e}")

# Build parser generator
parserGen = SConscript('parsergen/SConscript', exports=exp)

def parserGenEmitter(target, source, env):
    env.Depends(target, parserGen)
    return (target, source)
    
def parserBuilderFunc(target, source, env):
    subprocess.run([parserGen[0].abspath, str(source[0]), str(target[0]) + '.re'], check=True)
    with open(str(target[0]), 'w') as out:
        subprocess.run(['re2c', '-Wno-useless-escape', '-Wno-unreachable-rules', str(target[0]) + '.re'], stdout=out, check=True)
    return None
    
parserBuilder = Builder(action=parserBuilderFunc,
                         emitter=parserGenEmitter,
                         suffix='.h', src_suffix='.pg')
    
env['BUILDERS']['Parser'] = parserBuilder

# Build the rest
for i in sconscript:
    SConscript(i + '/SConscript', exports=exp)

# Generate compile_commands.json for IDE/LSP support
env.CompilationDatabase()
