import re
import os
import subprocess
import json
import shutil

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
    'Vendor/xBRZ_1.9',
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

def detectClang(env):
    """Return (cc, cxx) absolute paths to a clang toolchain, or None.

    Used by fuzz SConscripts to force a clang/libFuzzer toolchain independent
    of the project's default gcc toolchain. If CXX already resolves to a
    clang binary, that is used; otherwise PATH is searched for clang++ /
    clang++-NN (highest version wins).
    """
    def basename(p):
        return os.path.basename(p) if p else ''

    cxx = env.get('CXX')
    cxx_path = shutil.which(cxx) if cxx else None
    if cxx_path and 'clang' in basename(cxx_path):
        cc = env.get('CC')
        cc_path = shutil.which(cc) if cc else None
        if not cc_path:
            cc_path = cxx_path.replace('clang++', 'clang')
        return (cc_path, cxx_path)

    # Search PATH for clang++ / clang++-NN; prefer the highest version.
    cands = []
    for d in os.environ.get('PATH', '').split(os.pathsep):
        if not d or not os.path.isdir(d):
            continue
        for f in os.listdir(d):
            m = re.match(r'^clang\+\+(?:-(\d+))?$', f)
            if m:
                cands.append((int(m.group(1) or 0), os.path.join(d, f)))
    if not cands:
        return None
    cands.sort(key=lambda t: t[0], reverse=True)
    cxxp = cands[0][1]
    ccp = cxxp.replace('clang++', 'clang')
    return (ccp, cxxp)

# Initialize Environment
env = Environment(ENV=os.environ.copy())

# Add custom methods
env.AddMethod(getBinName)
env.AddMethod(getLibName)
env.AddMethod(getObjects)
env.AddMethod(detectClang)

# Generate compile_commands.json for IDE/LSP support
env.Tool('compilation_db')

def envif(env_name, arguments, arg_name):
    val = arguments.get(arg_name, None)
    if val is not None:
        env[env_name] = val 

# Set custom variables
env['MY_CONF'] = ARGUMENTS.get('conf', 'posix')
_build_arg = ARGUMENTS.get('build', 'release')
# Dedicated server is now a runtime mode (`gusanos --dedicated`), not a
# separate compile target. Map the legacy build names to their client
# equivalents so old `build=dedserv` / `build=dedserv-debug` invocations
# keep working (same flags + object dir as release/debug) instead of
# silently producing a flagless binary in a stray object dir.
if _build_arg in ('dedserv', 'dedserv-debug'):
    _build_arg = 'release' if _build_arg == 'dedserv' else 'debug'
env['MY_BUILD'] = _build_arg
env['MY_SUBFOLDER'] = os.path.join(env['MY_CONF'], env['MY_BUILD'])
env['NO_PARSERS'] = ARGUMENTS.get('no-parsers', False)
env['BUILD_TESTS'] = ARGUMENTS.get('build-tests', '1') != '0'
env['FUZZ'] = ARGUMENTS.get('fuzz', '0') != '0'
env['CCCOMSTR'] = 'Compiling $TARGET'
env['CXXCOMSTR'] = 'Compiling $TARGET'
env['LINKCOMSTR'] = 'Linking $TARGET'
envif('CC', ARGUMENTS, 'CC')
envif('CXX', ARGUMENTS, 'CXX')
envif('LINK', ARGUMENTS, 'LINK')

# ---------------------------------------------------------------------------
# Sanitizer support
# ---------------------------------------------------------------------------
# Usage:
#   scons sanitize=address          # ASan only
#   scons sanitize=address,undefined # ASan + UBSan
#   scons sanitize=mem              # MSan (requires clang)
#   scons sanitize=none             # explicit disable (default)
#
# Accepted aliases: address/asan/a, undefined/ubsan, thread/tsan, mem/memory
# Sanitizers require -Og; build-type optimization flags are overridden.
# ---------------------------------------------------------------------------
_sanitize_arg = ARGUMENTS.get('sanitize', 'none')
if _sanitize_arg and _sanitize_arg != 'none':
    _sanitize_list = [s.strip() for s in _sanitize_arg.split(',') if s.strip()]
    _valid_sanitizers = {
        'address': 'address', 'asan': 'address',
        'undefined': 'undefined', 'ubsan': 'undefined',
        'thread': 'thread', 'tsan': 'thread',
        'memory': 'memory', 'msan': 'memory',
    }
    _resolved = []
    for s in _sanitize_list:
        key = s.lower()
        if key in _valid_sanitizers:
            _resolved.append(_valid_sanitizers[key])
        else:
            print(f"Warning: Unknown sanitizer '{s}', passing through as-is")
            _resolved.append(s)
    _san_flags = ' '.join(f'-fsanitize={s}' for s in _resolved)
    env.Append(CCFLAGS=Split(_san_flags), LINKFLAGS=Split(_san_flags))
    # Remove optimization flags that conflict with sanitizers
    env['CCFLAGS'] = [f for f in env['CCFLAGS']
                      if not f.startswith('-O') or f == '-Og']
    env.Append(CCFLAGS='-Og')


# Homebrew
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
    CPPPATH=Split('. #http #luaapi #Console #GUI #Utility #OmfgScript #Goop #Net #Vendor/xBRZ_1.9'),
    LIBPATH=[os.path.join('#lib', env['MY_SUBFOLDER']), os.path.join('#lib', env['MY_CONF'])],
    # -fno-diagnostics-show-caret 
    CCFLAGS=Split('-pipe -fno-diagnostics-show-option -Wfatal-errors -Wall -Wno-unused -Wno-register -Wno-implicit-fallthrough'),
    CXXFLAGS=Split('-std=c++17'),
    CPPDEFINES=['_GNU_SOURCE', 'BOOST_TIMER_ENABLE_DEPRECATED']
)

if env['MY_BUILD'] == 'release':
    env.Append(CCFLAGS=Split('-O3 -g'),
               CPPDEFINES=['NDEBUG'])
elif env['MY_BUILD'] == 'debug':
    env.Append(CCFLAGS=Split('-Og -g -fno-omit-frame-pointer -Wextra'),
               CPPDEFINES=['DEBUG', 'MAP_DOWNLOADING', 'LOG_RUNTIME'])

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
cdb = env.CompilationDatabase()

# ---------------------------------------------------------------------------
# clang-format / clang-tidy targets
# ---------------------------------------------------------------------------
# Sources scanned for both tools. Bundled third-party code, generated parser
# headers, and build artifacts are excluded.
# ---------------------------------------------------------------------------
_FORMAT_TIDY_SOURCE_DIRS = [
    'Goop', 'Net', 'Console', 'GUI', 'Utility/util',
    'OmfgScript', 'http', 'luaapi', 'lighter', 'liero2gus', 'parsergen',
]

# Directories to prune while walking source trees.
_FORMAT_TIDY_EXCLUDED_DIRS = {'.build', 'bin', 'lib', '.sconf_temp', '.git'}

# Specific paths (relative to project root) to skip entirely.
_FORMAT_TIDY_EXCLUDED_PATHS = {
    'Net/Reference',  # ZoidCom reference samples, not part of the Gusanos build
}

_FORMAT_TIDY_GENERATED_HEADERS = {
    'Console/console-grammar.h',
    'OmfgScript/omfg_script_parser.h',
    'GUI/detail/gss-grammar.h',
}

_FORMAT_TIDY_SOURCE_EXTS = {'.cpp', '.h', '.hpp', '.c'}

def _collect_project_sources(env):
    sources = []
    for d in _FORMAT_TIDY_SOURCE_DIRS:
        if not os.path.isdir(d):
            continue
        for dirpath, dirnames, filenames in os.walk(d):
            dirnames[:] = [dn for dn in dirnames if dn not in _FORMAT_TIDY_EXCLUDED_DIRS]
            # Skip excluded whole subtrees (e.g. reference samples).
            rel_dir = os.path.relpath(dirpath, '.')
            if rel_dir in _FORMAT_TIDY_EXCLUDED_PATHS:
                dirnames[:] = []
                continue
            for fn in filenames:
                if os.path.splitext(fn)[1] in _FORMAT_TIDY_SOURCE_EXTS:
                    path = os.path.join(dirpath, fn)
                    if path not in _FORMAT_TIDY_GENERATED_HEADERS:
                        sources.append(path)
    return sorted(set(sources))

def _run_clang_format(target, source, env):
    files = _collect_project_sources(env)
    if not files:
        print("No source files found for formatting")
        return 0
    try:
        subprocess.run(['clang-format', '-i'] + files, check=True)
    except FileNotFoundError:
        print("Error: clang-format not found in PATH")
        return 1
    return None

def _load_compile_db_cpp_files():
    try:
        with open('compile_commands.json') as f:
            return {entry['file'] for entry in json.load(f)
                    if entry['file'].endswith('.cpp')}
    except Exception:
        return set()

_CLANG_TIDY = os.environ.get('CLANG_TIDY', 'clang-tidy')

def _run_clang_tidy(target, source, env):
    all_cpp = [f for f in _collect_project_sources(env) if f.endswith('.cpp')]
    cdb_files = _load_compile_db_cpp_files()
    files = [f for f in all_cpp if f in cdb_files]
    skipped = [f for f in all_cpp if f not in cdb_files]
    if skipped:
        print(f"Skipping {len(skipped)} file(s) not in compile_commands.json")
    if not files:
        print("No C++ source files found for tidy")
        return 0
    print(f"Running {_CLANG_TIDY} on {len(files)} file(s)...")
    try:
        subprocess.run([_CLANG_TIDY, '-p', '.'] + files, check=True)
    except FileNotFoundError:
        print(f"Error: {_CLANG_TIDY} not found in PATH")
        return 1
    except subprocess.CalledProcessError as e:
        # emit-only: report diagnostics but do not fail the scons build
        print(f"clang-tidy finished with exit code {e.returncode}; ignoring because tidy is emit-only")
    return 0

_format_target = env.Alias('format', [], _run_clang_format)
_tidy_target = env.Alias('tidy', [], _run_clang_tidy)
env.Depends(_tidy_target, cdb)
env.AlwaysBuild(_format_target)
env.AlwaysBuild(_tidy_target)
