# Released under the MIT License. See LICENSE for details.
#
"""A nice collection of ready-to-use pcommands for this package."""
from __future__ import annotations

# Note: import as little as possible here at the module level to
# keep launch times fast for small snippets.
import sys

from efrotools import pcommand


def gen_monolithic_register_modules() -> None:
    """Generate .h file for registering py modules."""
    import os
    import textwrap

    from efro.error import CleanError
    from batools.featureset import FeatureSet

    pcommand.disallow_in_batch()

    if len(sys.argv) != 3:
        raise CleanError('Expected 1 arg.')
    outpath = sys.argv[2]

    featuresets = FeatureSet.get_all_for_project(str(pcommand.PROJROOT))

    # Filter out ones without native modules.
    featuresets = [f for f in featuresets if f.has_python_binary_module]

    pymodulenames = sorted(f.name_python_binary_module for f in featuresets)

    def initname(mname: str) -> str:
        # plus is a special case since we need to define that symbol
        # ourself.
        return f'DoPyInit_{mname}' if mname == '_baplus' else f'PyInit_{mname}'

    extern_def_code = '\n'.join(
        f'auto {initname(n)}() -> PyObject*;' for n in pymodulenames
    )

    py_register_code = '\n'.join(
        f'PyImport_AppendInittab("{n}", &{initname(n)});' for n in pymodulenames
    )

    if '_baplus' in pymodulenames:
        init_plus_code = (
            '\n'
            '// Slight hack: because we are currently building baplus as a'
            ' static module\n'
            '// and linking it in, symbols exported there (namely'
            ' PyInit__baplus) do not\n'
            '// seem to be available through us when we are compiled as'
            ' a dynamic\n'
            '// library. This leads to Python being unable to load baplus.'
            ' While I\'m sure\n'
            '// there is some way to get those symbols exported, I\'m worried'
            ' it might be\n'
            '// a messy platform-specific affair. So instead we\'re just'
            ' defining that\n'
            '// function here when baplus is present and forwarding it through'
            ' to the\n'
            '// static library version.\n'
            'extern "C" auto PyInit__baplus() -> PyObject* {\n'
            '  return DoPyInit__baplus();\n'
            '}\n'
        )
    else:
        init_plus_code = ''

    base_code = """
        // Released under the MIT License. See LICENSE for details.

        #ifndef BALLISTICA_CORE_MGEN_PYTHON_MODULES_MONOLITHIC_H_
        #define BALLISTICA_CORE_MGEN_PYTHON_MODULES_MONOLITHIC_H_

        // THIS CODE IS AUTOGENERATED BY META BUILD; DO NOT EDIT BY HAND.

        #include "ballistica/shared/ballistica.h"
        #include "ballistica/shared/python/python_sys.h"

        extern "C" {
        ${EXTERN_DEF_CODE}
        }

        namespace ballistica {

        /// Register init calls for all of our built-in Python modules.
        /// Should only be used in monolithic builds. In modular builds
        /// binary modules get located as .so files on disk as per regular
        /// Python behavior.
        void MonolithicRegisterPythonModules() {
          if (g_buildconfig.monolithic_build()) {
        ${PY_REGISTER_CODE}
          } else {
            FatalError(
                "MonolithicRegisterPythonModules should not be called"
                " in modular builds.");
          }
        }
        ${PY_INIT_PLUS}
        }  // namespace ballistica

        #endif  // BALLISTICA_CORE_MGEN_PYTHON_MODULES_MONOLITHIC_H_
        """
    out = (
        textwrap.dedent(base_code)
        .replace('${EXTERN_DEF_CODE}', extern_def_code)
        .replace(
            '${PY_REGISTER_CODE}', textwrap.indent(py_register_code, '    ')
        )
        .replace('${PY_INIT_PLUS}', init_plus_code)
        .strip()
        + '\n'
    )

    os.makedirs(os.path.dirname(outpath), exist_ok=True)
    with open(outpath, 'w', encoding='utf-8') as outfile:
        outfile.write(out)


def py_examine() -> None:
    """Run a python examination at a given point in a given file."""
    import os
    from pathlib import Path
    import efrotools

    pcommand.disallow_in_batch()

    if len(sys.argv) != 7:
        print('ERROR: expected 7 args')
        sys.exit(255)
    filename = Path(sys.argv[2])
    line = int(sys.argv[3])
    column = int(sys.argv[4])
    selection: str | None = None if sys.argv[5] == '' else sys.argv[5]
    operation = sys.argv[6]

    # This stuff assumes it is being run from project root.
    os.chdir(pcommand.PROJROOT)

    # Set up pypaths so our main distro stuff works.
    scriptsdir = os.path.abspath(
        os.path.join(
            os.path.dirname(sys.argv[0]), '../src/assets/ba_data/python'
        )
    )
    toolsdir = os.path.abspath(
        os.path.join(os.path.dirname(sys.argv[0]), '../tools')
    )
    if scriptsdir not in sys.path:
        sys.path.append(scriptsdir)
    if toolsdir not in sys.path:
        sys.path.append(toolsdir)
    efrotools.py_examine(
        pcommand.PROJROOT, filename, line, column, selection, operation
    )


def clean_orphaned_assets() -> None:
    """Remove asset files that are no longer part of the build."""
    import os
    import json
    import subprocess

    pcommand.disallow_in_batch()

    # Operate from dist root..
    os.chdir(pcommand.PROJROOT)

    # Our manifest is split into 2 files (public and private)
    with open(
        'src/assets/.asset_manifest_public.json', encoding='utf-8'
    ) as infile:
        manifest = set(json.loads(infile.read()))
    with open(
        'src/assets/.asset_manifest_private.json', encoding='utf-8'
    ) as infile:
        manifest.update(set(json.loads(infile.read())))
    for root, _dirs, fnames in os.walk('build/assets'):
        for fname in fnames:
            fpath = os.path.join(root, fname)
            fpathrel = fpath[13:]  # paths are relative to build/assets
            if fpathrel not in manifest:
                print(f'Removing orphaned asset file: {fpath}')
                os.unlink(fpath)

    # Lastly, clear empty dirs.
    subprocess.run(
        'find build/assets -depth -empty -type d -delete',
        shell=True,
        check=True,
    )


def win_ci_install_prereqs() -> None:
    """Install bits needed for basic win ci."""
    import json

    from efrotools.efrocache import get_target

    pcommand.disallow_in_batch()

    # We'll need to pull a handful of things out of efrocache for the
    # build to succeed. Normally this would happen through our Makefile
    # targets but we can't use them under raw window so we need to just
    # hard-code whatever we need here.
    lib_dbg_win32 = 'build/prefab/lib/windows/Debug_Win32'
    needed_targets: set[str] = {
        f'{lib_dbg_win32}/BallisticaKitGenericPlus.lib',
        f'{lib_dbg_win32}/BallisticaKitGenericPlus.pdb',
        'ballisticakit-windows/Generic/BallisticaKit.ico',
    }

    # Look through everything that gets generated by our meta builds
    # and pick out anything we need for our basic builds/tests.
    with open(
        'src/meta/.meta_manifest_public.json', encoding='utf-8'
    ) as infile:
        meta_public: list[str] = json.loads(infile.read())
    with open(
        'src/meta/.meta_manifest_private.json', encoding='utf-8'
    ) as infile:
        meta_private: list[str] = json.loads(infile.read())
    for target in meta_public + meta_private:
        if (target.startswith('src/ballistica/') and '/mgen/' in target) or (
            target.startswith('src/assets/ba_data/python/')
            and '/_mgen/' in target
        ):
            needed_targets.add(target)

    for target in needed_targets:
        get_target(target, batch=pcommand.is_batch(), clr=pcommand.clr())


def win_ci_binary_build() -> None:
    """Simple windows binary build for ci."""
    import subprocess

    pcommand.disallow_in_batch()

    # Do the thing.
    subprocess.run(
        [
            'C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\'
            'Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe',
            'ballisticakit-windows\\Generic\\BallisticaKitGeneric.vcxproj',
            '-target:Build',
            '-property:Configuration=Debug',
            '-property:Platform=Win32',
            '-property:VisualStudioVersion=16',
        ],
        check=True,
    )


def update_cmake_prefab_lib() -> None:
    """Update prefab internal libs for builds."""
    import subprocess
    import os
    from efro.error import CleanError
    import batools.build

    pcommand.disallow_in_batch()

    if len(sys.argv) != 5:
        raise CleanError(
            'Expected 3 args (standard/server, debug/release, build-dir)'
        )
    buildtype = sys.argv[2]
    mode = sys.argv[3]
    builddir = sys.argv[4]
    if buildtype not in {'standard', 'server'}:
        raise CleanError(f'Invalid buildtype: {buildtype}')
    if mode not in {'debug', 'release'}:
        raise CleanError(f'Invalid mode: {mode}')
    platform = batools.build.get_current_prefab_platform(
        wsl_gives_windows=False
    )
    suffix = '_server' if buildtype == 'server' else '_gui'
    target = f'build/prefab/lib/{platform}{suffix}/{mode}/libballisticaplus.a'

    # Build the target and then copy it to dst if it doesn't exist there yet
    # or the existing one is older than our target.
    subprocess.run(['make', target], check=True)

    libdir = os.path.join(builddir, 'prefablib')
    libpath = os.path.join(libdir, 'libballisticaplus.a')

    update = True
    time1 = os.path.getmtime(target)
    if os.path.exists(libpath):
        time2 = os.path.getmtime(libpath)
        if time1 <= time2:
            update = False

    if update:
        if not os.path.exists(libdir):
            os.makedirs(libdir, exist_ok=True)
        subprocess.run(['cp', target, libdir], check=True)


def android_archive_unstripped_libs() -> None:
    """Copy libs to a build archive."""
    import subprocess
    from pathlib import Path
    from efro.error import CleanError
    from efro.terminal import Clr

    pcommand.disallow_in_batch()

    if len(sys.argv) != 4:
        raise CleanError('Expected 2 args; src-dir and dst-dir')
    src = Path(sys.argv[2])
    dst = Path(sys.argv[3])
    if dst.exists():
        subprocess.run(['rm', '-rf', dst], check=True)
    dst.mkdir(parents=True, exist_ok=True)
    if not src.is_dir():
        raise CleanError(f"Source dir not found: '{src}'")
    libname = 'libmain'
    libext = '.so'
    for abi, abishort in [
        ('armeabi-v7a', 'arm'),
        ('arm64-v8a', 'arm64'),
        ('x86', 'x86'),
        ('x86_64', 'x86-64'),
    ]:
        srcpath = Path(src, abi, libname + libext)
        dstname = f'{libname}_{abishort}{libext}'
        dstpath = Path(dst, dstname)
        if srcpath.exists():
            print(f'Archiving unstripped library: {Clr.BLD}{dstname}{Clr.RST}')
            subprocess.run(['cp', srcpath, dstpath], check=True)
            subprocess.run(
                ['tar', '-zcf', dstname + '.tgz', dstname], cwd=dst, check=True
            )
            subprocess.run(['rm', dstpath], check=True)


def spinoff_test() -> None:
    """Test spinoff functionality."""
    import batools.spinoff

    batools.spinoff.spinoff_test(sys.argv[2:])


def spinoff_check_submodule_parent() -> None:
    """Make sure this dst proj has a submodule parent."""
    import os
    from efro.error import CleanError

    pcommand.disallow_in_batch()

    # Make sure we're a spinoff dst project. The spinoff command will be
    # a symlink if this is the case.
    if not os.path.exists('tools/spinoff'):
        raise CleanError(
            'This does not appear to be a spinoff-enabled project.'
        )
    if not os.path.islink('tools/spinoff'):
        raise CleanError('This project is a spinoff parent; we require a dst.')

    if not os.path.isdir('submodules/ballistica'):
        raise CleanError(
            'This project is not using a submodule for its parent.\n'
            'To set one up, run `tools/spinoff add-submodule-parent`'
        )


def gen_python_init_module() -> None:
    """Generate a basic __init__.py."""
    import os

    from efro.error import CleanError
    from efro.terminal import Clr

    from batools.project import project_centric_path

    pcommand.disallow_in_batch()

    if len(sys.argv) != 3:
        raise CleanError('Expected an outfile arg.')
    outfilename = sys.argv[2]
    os.makedirs(os.path.dirname(outfilename), exist_ok=True)
    prettypath = project_centric_path(
        projroot=str(pcommand.PROJROOT), path=outfilename
    )
    print(f'Meta-building {Clr.BLD}{prettypath}{Clr.RST}')
    with open(outfilename, 'w', encoding='utf-8') as outfile:
        outfile.write(
            '# Released under the MIT License.'
            ' See LICENSE for details.\n'
            '#\n'
        )


def tests_warm_start() -> None:
    """Warm-start some stuff needed by tests.

    This keeps logs clearer by showing any binary builds/downloads we
    need to do instead of having those silently happen as part of
    tests.
    """
    from batools import apprun

    pcommand.disallow_in_batch()

    # We do lots of apprun.python_command() within test. Pre-build the
    # binary that they need to do their thing.
    if not apprun.test_runs_disabled():
        apprun.acquire_binary_for_python_command(purpose='running tests')


def wsl_build_check_win_drive() -> None:
    """Make sure we're building on a windows drive."""
    import os
    import subprocess
    import textwrap
    from efro.error import CleanError

    # We use env vars to influence our behavior and thus can't support
    # batch.
    pcommand.disallow_in_batch()

    if (
        subprocess.run(
            ['which', 'wslpath'], check=False, capture_output=True
        ).returncode
        != 0
    ):
        raise CleanError(
            'wslpath not found; you must run this from a WSL environment'
        )

    if os.environ.get('WSL_BUILD_CHECK_WIN_DRIVE_IGNORE') == '1':
        return

    # Get a windows path to the current dir.
    path = (
        subprocess.run(
            ['wslpath', '-w', '-a', os.getcwd()],
            capture_output=True,
            check=True,
        )
        .stdout.decode()
        .strip()
    )

    # If we're sitting under the linux filesystem, our path
    # will start with \\wsl$; fail in that case and explain why.
    if not path.startswith('\\\\wsl$'):
        return

    def _wrap(txt: str) -> str:
        return textwrap.fill(txt, 76)

    raise CleanError(
        '\n\n'.join(
            [
                _wrap(
                    'ERROR: This project appears to live'
                    ' on the Linux filesystem.'
                ),
                _wrap(
                    'Visual Studio compiles will error here for reasons related'
                    ' to Linux filesystem case-sensitivity, and thus are'
                    ' disallowed.'
                    ' Clone the repo to a location that maps to a native'
                    ' Windows drive such as \'/mnt/c/ballistica\''
                    ' and try again.'
                ),
                _wrap(
                    'Note that WSL2 filesystem performance'
                    ' is poor when accessing'
                    ' native Windows drives, so if Visual Studio builds are not'
                    ' needed it may be best to keep things'
                    ' on the Linux filesystem.'
                    ' This behavior may differ under WSL1 (untested).'
                ),
                _wrap(
                    'Set env-var WSL_BUILD_CHECK_WIN_DRIVE_IGNORE=1 to skip'
                    ' this check.'
                ),
            ]
        )
    )


def wsl_path_to_win() -> None:
    """Forward escape slashes in a provided win path arg."""
    import subprocess
    import logging
    import os
    from efro.error import CleanError

    pcommand.disallow_in_batch()

    try:
        create = False
        escape = False
        if len(sys.argv) < 3:
            raise CleanError('Expected at least 1 path arg.')
        wsl_path: str | None = None
        for arg in sys.argv[2:]:
            if arg == '--create':
                create = True
            elif arg == '--escape':
                escape = True
            else:
                if wsl_path is not None:
                    raise CleanError('More than one path provided.')
                wsl_path = arg
        if wsl_path is None:
            raise CleanError('No path provided.')

        # wslpath fails on nonexistent paths; make it clear when that happens.
        if create:
            os.makedirs(wsl_path, exist_ok=True)
        if not os.path.exists(wsl_path):
            raise CleanError(f'Path \'{wsl_path}\' does not exist.')

        results = subprocess.run(
            ['wslpath', '-w', '-a', wsl_path], capture_output=True, check=True
        )
    except Exception:
        # This gets used in a makefile so our returncode is ignored;
        # let's try to make our failure known in other ways.
        logging.exception('wsl_to_escaped_win_path failed.')
        print('wsl_to_escaped_win_path_error_occurred', end='')
        return

    out = results.stdout.decode().strip()

    # If our input ended with a slash, match in the output.
    if wsl_path.endswith('/') and not out.endswith('\\'):
        out += '\\'

    if escape:
        out = out.replace('\\', '\\\\')
    print(out, end='')
