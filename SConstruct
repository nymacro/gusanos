import re
import os
import subprocess

exp = Split('env')
sconscript = [
    'GUI',
    'Utility/util',
    'Console',
    'loadpng',
    'Goop',
    'OmfgScript',
    'liero2gus',
    'lua51',
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

# Set custom variables
env['MY_CONF'] = ARGUMENTS.get('conf', 'posix')
env['MY_BUILD'] = ARGUMENTS.get('build', 'release')
env['MY_SUBFOLDER'] = os.path.join(env['MY_CONF'], env['MY_BUILD'])
env['NO_PARSERS'] = ARGUMENTS.get('no-parsers', False)

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
    CPPPATH=Split('. #http #loadpng #lua51 #Console #GUI #Utility #OmfgScript'),
    LIBPATH=[os.path.join('#lib', env['MY_SUBFOLDER']), os.path.join('#lib', env['MY_CONF'])],
    CPPFLAGS=Split('-pipe -Wall -Wno-reorder -std=c++17')
)

if env['MY_BUILD'] == 'release':
    env.Append(CPPFLAGS=Split('-O3 -g -DNDEBUG -fomit-frame-pointer'))
elif env['MY_BUILD'] == 'debug':
    env.Append(CPPFLAGS=Split('-O0 -g -DDEBUG -DMAP_DOWNLOADING -DLOG_RUNTIME'))
elif env['MY_BUILD'] == 'dedserv':
    env.Append(CPPFLAGS=Split('-O3 -g -DNDEBUG -DDEDSERV -fomit-frame-pointer'))
elif env['MY_BUILD'] == 'dedserv-debug':
    env.Append(CPPFLAGS=Split('-O0 -g -DDEBUG -DDEDSERV -DLOG_RUNTIME'))

# Dependency Detection
libs = ['sdl3', 'sdl3-mixer', 'sdl3-ttf', 'libenet', 'libpng', 'zlib']
for lib in libs:
    try:
        env.ParseConfig(f'pkg-config --cflags --libs {lib}')
    except Exception as e:
        print(f"Warning: Could not find {lib} via pkg-config. Error: {e}")

# Boost Detection
boost_libs = ['boost_filesystem', 'boost_system']
for blib in boost_libs:
    if not env.GetOption('clean'):
        conf = Configure(env)
        if not conf.CheckLib(blib, language='C++'):
            print(f"Warning: Could not find boost library {blib}")
        env = conf.Finish()

# Build parser generator
parserGen = SConscript('parsergen/SConscript', exports=exp)

def parserGenEmitter(target, source, env):
    env.Depends(target, parserGen)
    return (target, source)
    
def parserBuilderFunc(target, source, env):
    subprocess.run([parserGen[0].abspath, str(source[0]), str(target[0]) + '.re'], check=True)
    with open(str(target[0]), 'w') as out:
        subprocess.run(['re2c', str(target[0]) + '.re'], stdout=out, check=True)
    return None
    
parserBuilder = Builder(action=parserBuilderFunc,
                         emitter=parserGenEmitter,
                         suffix='.h', src_suffix='.pg')
    
env['BUILDERS']['Parser'] = parserBuilder

# Build the rest
for i in sconscript:
    SConscript(i + '/SConscript', exports=exp)
