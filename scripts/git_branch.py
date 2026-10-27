"""
PlatformIO pre-build script: inject CROSSPOINT_VERSION as MAJOR.MINOR.YYYYMMDD,
where MAJOR.MINOR comes from [crosspoint] version and the date is the build day.

Release builds:      1.4.20261027
Development builds:  1.4.20261027-dev-feat-kosync-xpath-05c6cf8
The RC environment keeps its own CROSSPOINT_VERSION from the ini.
"""

import configparser
import os
import subprocess
import sys
import time


def warn(msg):
    print(f'WARNING [git_branch.py]: {msg}', file=sys.stderr)


def run_git_value(project_dir, args, label):
    try:
        value = subprocess.check_output(
            ['git', *args],
            text=True, stderr=subprocess.PIPE, cwd=project_dir
        ).strip()
        # Strip characters that would break a C string literal
        return ''.join(c for c in value if c not in '"\\')
    except FileNotFoundError:
        warn(f'git not found on PATH; {label} suffix will be "unknown"')
        return 'unknown'
    except subprocess.CalledProcessError as e:
        warn(
            f'git command failed (exit {e.returncode}): '
            f'{e.stderr.strip()}; {label} suffix will be "unknown"'
        )
        return 'unknown'
    except OSError as e:
        warn(
            f'OS error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'
    except Exception as e:  # pylint: disable=broad-exception-caught
        warn(
            f'Unexpected error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'


def get_git_branch(project_dir):
    branch = run_git_value(
        project_dir, ['rev-parse', '--abbrev-ref', 'HEAD'], 'branch'
    )
    # Detached HEAD has no branch name.
    if branch == 'HEAD':
        return 'detached'
    return branch


def get_git_short_sha(project_dir):
    return run_git_value(
        project_dir, ['rev-parse', '--short', 'HEAD'], 'short SHA'
    )


def get_base_version(project_dir):
    ini_path = os.path.join(project_dir, 'platformio.ini')
    if not os.path.isfile(ini_path):
        warn(f'platformio.ini not found at {ini_path}; base version will be "0.0.0"')
        return '0.0.0'
    config = configparser.ConfigParser()
    config.read(ini_path, encoding='utf-8')
    if not config.has_option('crosspoint', 'version'):
        warn('No [crosspoint] version in platformio.ini; base version will be "0.0.0"')
        return '0.0.0'
    return config.get('crosspoint', 'version')


def inject_version(env):
    project_dir = env['PROJECT_DIR']

    if env['PIOENV'] != 'gh_release_rc':
        base_version = f'{get_base_version(project_dir)}.{time.strftime("%Y%m%d")}'
        if env['PIOENV'] == 'default':
            branch = get_git_branch(project_dir)
            short_sha = get_git_short_sha(project_dir)
            version_string = f'{base_version}-dev-{branch}-{short_sha}'
        else:
            version_string = base_version
        env.Append(CPPDEFINES=[
            ('CROSSPOINT_VERSION', f'\\\"{version_string}\\\"'),
        ])
        print(f'CrossPoint build version: {version_string}')

    # Every firmware build gets a unique cache ID, including repeated builds
    # of the same Git commit.
    build_id = str(time.time_ns())
    env.Append(CPPDEFINES=[
        ('BOOK_CACHE_BUILD_ID', f'\\\"{build_id}\\\"'),
    ])
    print(f'Book cache build ID: {build_id}')


# PlatformIO/SCons entry point — Import and env are SCons builtins injected at runtime.
# When run directly with Python (e.g. for validation), a lightweight fake env is used
# so the git/version logic can be exercised without a full build.
try:
    Import('env')           # noqa: F821  # type: ignore[name-defined]
    inject_version(env)     # noqa: F821  # type: ignore[name-defined]
except NameError:
    class _Env(dict):
        def Append(self, **_): pass

    _project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    inject_version(_Env({'PIOENV': 'default', 'PROJECT_DIR': _project_dir}))
