#!/usr/bin/env python3
"""Star Fighter regression test runner (Python 3.6+, standard library only).

    tests/run.py [options] quick          build checks, unit tests, quick scenarios, determinism, SDL smoke
    tests/run.py [options] asan           unit tests + quick scenarios built with AddressSanitizer + UBSan
    tests/run.py [options] full           quick + asan + every scenario (incl. long ones), also under ASan
    tests/run.py [options] rebless [SCENARIO ...]   re-record goldens (all scenarios if none given)
    tests/run.py [options] make-fixtures  re-create tests/fixtures/base.save by playing the save scenario
    tests/run.py [options] performance    frames per second of the headless build, per scenario
    tests/run.py list                     list scenarios

Options:
    --resources DIR   SF_Resources directory (default: $SF_RESOURCES, else ./SF_Resources)
    --only TEXT       run only checks/scenarios whose name contains TEXT
    -j N              parallel jobs (default: number of CPUs)
    --keep            keep large run files (audio WAVs)
    --repeat N        performance: runs per scenario (default 3)

Results go to build/test-report/ (index.html, summary.txt, images).  See tests/README.md.
"""
import argparse
import concurrent.futures as cf
import hashlib
import html
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(ROOT, 'tests')
BUILD = os.path.join(ROOT, 'build')
REPORT = os.path.join(BUILD, 'test-report')
SCENARIOS = os.path.join(TESTS, 'scenarios')
GOLDEN = os.path.join(TESTS, 'golden')
FIXTURES = os.path.join(TESTS, 'fixtures')
UBSAN_SUPP = os.path.join(TESTS, 'ubsan.supp')
CLANG_BASELINE = os.path.join(TESTS, 'clang-warnings.txt')

# 32-bit casts into owner fields that are known to be safe (the value is a ground grid
# reference, never a pointer); anything else matching the lint pattern fails
LINT_ALLOWED = {
    ('Ground_Control.c', 'pleb_ship->who_owns_me = (int32_t) *found;'):
        'grid reference from scan_poly_map, not a pointer',
}

# Implicit 64->32 bit narrowings (clang -Wshorten-64-to-32) that are known to be safe: each takes
# a ground grid reference (< 16384) out of an intptr_t that holds either a grid reference or a
# pointer.  Any other narrowing fails the check (add_sam's owner was one: a crash in the demo).
SHORTEN_ALLOWED = {
    ('Draw_Land.c', 'y_grid = (*found) >> 7;'),
    ('Ground_Control.c', 'y_grid = (*found) >> 7;'),
    ('Laser_Control.c', 'y_grid = (((intptr_t) players_ship->target) >> 7);'),
    ('Weapons.c', 'y_grid = (((intptr_t) ship->target) >> 7);'),
}

LEVELS = {'T': 0, 'E': 1, 'M': 2, 'H': 3}
REQUIRED_FLAGS = ['-funsigned-char', '-fwrapv', '-fno-strict-aliasing']
TEST_CFLAGS = '-O2 -g -ffp-contract=off'

# build variants: name -> (make variables, binary)
VARIANTS = {
    'headless': (['HEADLESS=1', 'WERROR=1', 'CFLAGS=' + TEST_CFLAGS], 'starfighter-headless'),
    # -O1 makes gcc's format-truncation analysis noisier; the headless build is the warning gate
    'asan': (['HEADLESS=1', 'SAN=address,undefined',
              'CFLAGS=-O1 -g -ffp-contract=off -Wno-format-truncation'],
             'starfighter-headless'),
    'sdl': (['WERROR=1', 'CFLAGS=' + TEST_CFLAGS], 'starfighter'),
    # clang reports warnings gcc does not; they are compared with tests/clang-warnings.txt instead
    'clang': (['HEADLESS=1', 'CC=clang', 'CFLAGS=' + TEST_CFLAGS], 'starfighter-headless'),
}

PASS, FAIL, SKIP, WARN = 'PASS', 'FAIL', 'SKIP', 'WARN'


# --------------------------------------------------------------------------------------------------
# results
class Result:
    def __init__(self, group, name, status, detail='', secs=0.0, images=None):
        self.group, self.name, self.status, self.detail = group, name, status, detail
        self.secs, self.images = secs, images or []


class Results:
    def __init__(self):
        self.items = []

    def add(self, r):
        self.items.append(r)
        mark = {PASS: 'ok  ', FAIL: 'FAIL', SKIP: 'skip', WARN: 'warn'}[r.status]
        line = '  %s %-14s %s' % (mark, r.group, r.name)
        if r.secs >= 1:
            line += '  (%.0fs)' % r.secs
        if r.status != PASS and r.detail:
            first = r.detail.strip().splitlines()[0] if r.detail.strip() else ''
            line += '  - ' + first[:150]
        print(line, flush=True)
        return r

    def count(self, status):
        return sum(1 for r in self.items if r.status == status)


# --------------------------------------------------------------------------------------------------
# helpers
def run(cmd, cwd=ROOT, env=None, timeout=None):
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=timeout)
    return p.returncode, p.stdout.decode('utf-8', 'replace'), p.stderr.decode('utf-8', 'replace')


def which(prog):
    return shutil.which(prog) is not None


def clean_env(extra=None):
    """The environment for the game: the caller's, minus any SF_* settings."""
    env = {k: v for k, v in os.environ.items() if not k.startswith('SF_')}
    env['ASAN_OPTIONS'] = 'detect_leaks=0:abort_on_error=0'
    env['UBSAN_OPTIONS'] = 'print_stacktrace=1:suppressions=' + UBSAN_SUPP
    tmp = os.path.join(BUILD, 'tmp')
    os.makedirs(tmp, exist_ok=True)
    env['TMPDIR'] = tmp
    if extra:
        env.update(extra)
    return env


SAN_PATTERNS = re.compile(r'ERROR: AddressSanitizer|runtime error:|ERROR: LeakSanitizer|'
                          r'SUMMARY: UndefinedBehaviorSanitizer')


def sanitizer_report(text):
    m = SAN_PATTERNS.search(text)
    if not m:
        return None
    lines = text[m.start():].splitlines()
    return '\n'.join(lines[:25])


def ncpu():
    try:
        return len(os.sched_getaffinity(0))
    except AttributeError:
        return os.cpu_count() or 2


# --------------------------------------------------------------------------------------------------
# PNG / PPM (no third-party image library needed)
def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    # header: P6\n320 240\n255\n
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 6)))
        f.write(chunk(b'IEND', b''))


def read_png(path):
    """Reads the PNGs written by write_png (8-bit RGB, filter 0) only."""
    with open(path, 'rb') as f:
        data = f.read()
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(data):
        n, tag = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if tag == b'IHDR':
            w, h = struct.unpack('>II', body[:8])
        elif tag == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 3 + 1
    return w, h, b''.join(raw[y * stride + 1:(y + 1) * stride] for y in range(h))


def diff_image(a, b, w, h):
    """Grey version of b with differing pixels in red; returns (rgb, count)."""
    out = bytearray(w * h * 3)
    n = 0
    for i in range(0, w * h * 3, 3):
        if a[i:i + 3] != b[i:i + 3]:
            out[i], out[i + 1], out[i + 2] = 255, 0, 0
            n += 1
        else:
            g = (b[i] + b[i + 1] + b[i + 2]) // 6
            out[i] = out[i + 1] = out[i + 2] = g
    return bytes(out), n


def contact_sheet(frames, path, cols=6):
    """frames: list of (label, rgb 320x240).  Half-size thumbnails in a grid."""
    if not frames:
        return
    tw, th = 160, 120
    rows = (len(frames) + cols - 1) // cols
    W, H = cols * tw, rows * th
    out = bytearray(W * H * 3)
    for k, (_, rgb) in enumerate(frames):
        ox, oy = (k % cols) * tw, (k // cols) * th
        for y in range(th):
            src = rgb[(2 * y) * 320 * 3:(2 * y + 1) * 320 * 3]
            row = bytearray(tw * 3)
            row[0::3] = src[0::6]
            row[1::3] = src[1::6]
            row[2::3] = src[2::6]
            o = ((oy + y) * W + ox) * 3
            out[o:o + tw * 3] = row
    write_png(path, W, H, bytes(out))


# --------------------------------------------------------------------------------------------------
# resources manifest (decides whether goldens apply)
def resources_manifest(res):
    cache_path = os.path.join(BUILD, 'resources-manifest.json')
    try:
        with open(cache_path) as f:
            cache = json.load(f)
    except (OSError, ValueError):
        cache = {}
    entries, newcache = [], {}
    for dirpath, dirnames, filenames in os.walk(res):
        dirnames.sort()
        for fn in sorted(filenames):
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(p, res).replace(os.sep, '/')
            st = os.stat(p)
            key = '%s|%d|%d' % (rel, st.st_size, int(st.st_mtime))
            digest = cache.get(key)
            if digest is None:
                h = hashlib.md5()
                with open(p, 'rb') as f:
                    for block in iter(lambda: f.read(1 << 20), b''):
                        h.update(block)
                digest = h.hexdigest()
            newcache[key] = digest
            entries.append('%s %d %s' % (rel.lower(), st.st_size, digest))
    os.makedirs(BUILD, exist_ok=True)
    with open(cache_path, 'w') as f:
        json.dump(newcache, f)
    return hashlib.sha256('\n'.join(sorted(entries)).encode()).hexdigest()[:16], len(entries)


def compiler_id():
    rc, out, _ = run(['cc', '--version'])
    return out.splitlines()[0].strip() if rc == 0 and out else 'unknown'


# --------------------------------------------------------------------------------------------------
# building
def build(variant, results, jobs, unit=False):
    makevars, binary = VARIANTS[variant]
    vdir = os.path.join(BUILD, variant)
    rel = os.path.relpath(vdir, ROOT)  # always the same relative spelling (see the Makefile)
    cmd = ['make', '-j%d' % jobs] + makevars + ['OBJDIR=' + os.path.join(rel, 'obj'),
                                                  'BINDIR=' + rel, 'all']
    if unit:
        cmd.append('unit-tests')
    if variant == 'clang':
        cmd.insert(1, '-B')  # warnings are only printed when files are compiled
    t0 = time.time()
    rc, out, err = run(cmd)
    secs = time.time() - t0
    warnings = sorted(set(l for l in (out + err).splitlines() if 'warning:' in l))
    if rc != 0:
        errs = [l for l in (out + err).splitlines() if 'error' in l.lower()]
        results.add(Result('build', variant, FAIL, '\n'.join(errs[:30]) or err[-3000:], secs))
        return None
    if variant == 'clang':
        check_clang_baseline(warnings, results, secs)
    elif warnings:
        results.add(Result('build', variant, WARN, '\n'.join(warnings[:30]), secs))
    else:
        results.add(Result('build', variant, PASS, '', secs))
    return os.path.join(vdir, binary)


def warning_key(line):
    # 'src/SF_Bonus.c:193:33: warning: ... [-Wdeprecated-non-prototype]' -> 'SF_Bonus.c -Wdeprecated-non-prototype'
    m = re.match(r'(?:.*/)?([^/:]+):\d+:\d+: warning: .*\[(-W[^\]]+)\]', line)
    return '%s %s' % (m.group(1), m.group(2)) if m else line


def check_clang_baseline(warnings, results, secs):
    counts = {}
    for w in warnings:
        counts[warning_key(w)] = counts.get(warning_key(w), 0) + 1
    base = {}
    try:
        with open(CLANG_BASELINE) as f:
            for line in f:
                line = line.split('#')[0].strip()
                if line:
                    key, _, n = line.rpartition(' ')
                    base[key] = int(n)
    except OSError:
        pass
    new = ['%s (%d, baseline %d)' % (k, n, base.get(k, 0)) for k, n in sorted(counts.items())
           if n > base.get(k, 0)]
    if new:
        results.add(Result('build', 'clang', FAIL, 'new clang warnings:\n' + '\n'.join(new) +
                           '\n\n' + '\n'.join(warnings[:20]), secs))
    elif counts:
        results.add(Result('build', 'clang', WARN, '%d known warnings (tests/clang-warnings.txt): %s'
                           % (sum(counts.values()), ', '.join(sorted(counts))), secs))
    else:
        results.add(Result('build', 'clang', PASS, '', secs))


def mingw_build(results, jobs):
    """The Windows build as made with mingw32-make from cmd.exe: a copy of the sources built with the
    Makefile's defaults, Windows detection on (OS=Windows_NT) and recipes run through
    tests/tools/cmd-shell.sh, which gives mkdir cmd.exe's rules.  Needs a MinGW cross compiler
    ($MINGW_CC, default i686-w64-mingw32-gcc) and SDL2's MinGW development package ($SDL2_MINGW: its
    i686-w64-mingw32 or x86_64-w64-mingw32 directory)."""
    cc = os.environ.get('MINGW_CC', 'i686-w64-mingw32-gcc')
    sdl = os.environ.get('SDL2_MINGW', '')
    name = 'mingw (%s, cmd.exe rules)' % cc
    if not which(cc):
        results.add(Result('build', name, SKIP, 'no MinGW cross compiler; set MINGW_CC'))
        return
    if not sdl or not os.path.isdir(os.path.join(sdl, 'include', 'SDL2')):
        results.add(Result('build', name, SKIP, 'set SDL2_MINGW to the i686-w64-mingw32 (or x86_64) '
                           'directory of the SDL2 MinGW development package'))
        return
    shell = os.path.join(TESTS, 'tools', 'cmd-shell.sh')
    os.chmod(shell, 0o755)  # may have lost its executable bit (e.g. applied as a patch)
    tree = os.path.join(BUILD, 'mingw')
    shutil.rmtree(tree, ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, 'src'), os.path.join(tree, 'src'))
    shutil.copy(os.path.join(ROOT, 'Makefile'), tree)
    cmd = ['make', '-j%d' % jobs, 'SHELL=' + shell,
           'OS=Windows_NT', 'CC=' + cc, 'SDL2_PREFIX=' + sdl]
    t0 = time.time()
    rc, out, err = run(cmd, cwd=tree)
    secs = time.time() - t0
    if rc != 0 or not os.path.exists(os.path.join(tree, 'starfighter.exe')):
        errs = [l for l in (out + err).splitlines() if 'error' in l.lower() or 'incorrect' in l
                or 'already exists' in l]
        results.add(Result('build', name, FAIL, '\n'.join(errs[:20]) or (out + err)[-2000:], secs))
        return
    rc2, out2, err2 = run(cmd, cwd=tree)
    if rc2 != 0 or 'Nothing to be done' not in out2 + err2:
        results.add(Result('build', name, FAIL, 'a second make did not find everything up to date:\n'
                           + (out2 + err2)[-1500:], secs))
        return
    results.add(Result('build', name, PASS, '', secs))


def sdl_available():
    for c in (['sdl2-config', '--libs'], ['pkg-config', '--libs', 'sdl2']):
        try:
            if run(c)[0] == 0:
                return True
        except OSError:
            pass
    return False


def lint_checks(results):
    # 1. the flags the game depends on survive a command-line CFLAGS
    lint = os.path.join(BUILD, 'lint')
    lint = os.path.relpath(lint, ROOT)
    rc, out, err = run(['make', '-n', '-B', 'HEADLESS=1', 'CFLAGS=-O0', 'OBJDIR=' + lint + '/obj',
                        'BINDIR=' + lint, 'all'])
    compiles = [l for l in out.splitlines() if ' -c ' in l and '.c' in l]
    missing = [(l.split()[-1], f) for l in compiles for f in REQUIRED_FLAGS if f not in l.split()]
    if rc != 0 or not compiles:
        results.add(Result('lint', 'required compiler flags', FAIL, err[-2000:]))
    elif missing:
        results.add(Result('lint', 'required compiler flags', FAIL,
                           '\n'.join('%s lacks %s' % m for m in missing[:20])))
    else:
        results.add(Result('lint', 'required compiler flags', PASS,
                           'checked %d compile commands' % len(compiles)))
    # 2. owner fields must never be cut to 32 bits again (PORTING_NOTES section 9)
    pat = re.compile(r'\b(who_hit_me|who_owns_me|what_hit_me)\s*=\s*\(\s*(int32_t|int|long|uint32_t)\s*\)')
    hits = []
    for fn in sorted(os.listdir(os.path.join(ROOT, 'src'))):
        if fn.endswith('.c'):
            with open(os.path.join(ROOT, 'src', fn), errors='replace') as f:
                for i, line in enumerate(f, 1):
                    if pat.search(line) and (fn, line.strip()) not in LINT_ALLOWED:
                        hits.append('src/%s:%d: %s' % (fn, i, line.strip()))
    results.add(Result('lint', 'no 32-bit casts into owner pointers', FAIL if hits else PASS,
                       '\n'.join(hits)))
    # 3. no new implicit 64->32 bit narrowing (pointers stored in 32-bit integers)
    if not which('clang'):
        results.add(Result('lint', 'no implicit 64->32 bit narrowing', SKIP, 'clang not installed'))
        return
    srcs = sorted(f for f in os.listdir(os.path.join(ROOT, 'src'))
                  if f.endswith('.c') and f != 'plat_sdl.c')
    rc, out, err = run(['clang', '-fsyntax-only', '-funsigned-char', '-fwrapv', '-Isrc',
                        '-Wshorten-64-to-32'] + ['src/' + f for f in srcs])
    bad = []
    for line in err.splitlines():
        m = re.match(r'(?:.*/)?([^/:]+):(\d+):\d+: warning: .*\[-Wshorten-64-to-32\]', line)
        if not m:
            continue
        fn, ln = m.group(1), int(m.group(2))
        with open(os.path.join(ROOT, 'src', fn), errors='replace') as f:
            text = f.read().splitlines()[ln - 1].strip()
        if (fn, text) not in SHORTEN_ALLOWED:
            bad.append('src/%s:%d: %s' % (fn, ln, text))
    results.add(Result('lint', 'no implicit 64->32 bit narrowing', FAIL if bad else PASS,
                       '\n'.join(bad) if bad else '%d known-safe sites' % len(SHORTEN_ALLOWED)))


# --------------------------------------------------------------------------------------------------
# unit tests
def run_unit_tests(variant, results, jobs, res, only):
    unitdir = os.path.join(BUILD, variant, 'unit')
    if not os.path.isdir(unitdir):
        results.add(Result('unit/' + variant, 'unit test binaries', FAIL, 'not built'))
        return
    bins = sorted(f for f in os.listdir(unitdir)
                  if f.startswith('test_') and not f.endswith(('.o', '.d')))
    if only:
        bins = [b for b in bins if only in b]
    env = clean_env({'SF_RESOURCES': res})

    def one(b):
        t0 = time.time()
        try:
            rc, out, err = run([os.path.join(unitdir, b)], env=env, timeout=600)
        except subprocess.TimeoutExpired:
            return b, None, '', 'timed out after 600 s', time.time() - t0
        return b, rc, out, err, time.time() - t0

    with cf.ThreadPoolExecutor(jobs) as ex:
        outcomes = list(ex.map(one, bins))
    for b, rc, out, err, secs in outcomes:
        group = 'unit/' + variant
        if rc is None:
            results.add(Result(group, b, FAIL, err, secs))
            continue
        pending, seen, planned = [], 0, None
        for line in out.splitlines():
            m = re.match(r'(not ok|ok) \d+ - (\S+)', line)
            if m:
                seen += 1
                st = PASS if m.group(1) == 'ok' else FAIL
                results.add(Result(group, '%s: %s' % (b, m.group(2)), st, '\n'.join(pending)))
                pending = []
            elif line.startswith('#'):
                pending.append(line[1:].strip())
            elif re.match(r'1\.\.\d+', line):
                planned = int(line[3:])
        san = sanitizer_report(err)
        if san:
            results.add(Result(group, b + ': sanitizer', FAIL, san, secs))
        if planned is None or planned != seen or (rc != 0 and not any(
                r.status == FAIL for r in results.items if r.name.startswith(b + ':'))):
            results.add(Result(group, b + ': completed', FAIL,
                               'exit %s, %d of %s tests reported\n%s' % (rc, seen, planned, err[-2000:]),
                               secs))


# --------------------------------------------------------------------------------------------------
# scenarios
class Scenario:
    def __init__(self, path):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.description, self.suite, self.frames = '', 'quick', 0
        self.watchdog, self.fixture, self.resources = 300, None, None
        self.checkpoint, self.audio, self.golden = 300, True, True
        self.performance = False  # measured by 'performance' (plays in the 3D game engine)
        self.expects, self.script = [], []
        in_script = False
        with open(path) as f:
            for lineno, raw in enumerate(f, 1):
                line = raw.rstrip('\n')
                if in_script:
                    self.script.append(line)
                    continue
                s = line.strip()
                if not s or s.startswith('#'):
                    continue
                if s == 'script:':
                    in_script = True
                    continue
                key, _, val = s.partition(':')
                key, val = key.strip(), val.strip()
                if key == 'description':
                    self.description = val
                elif key == 'suite':
                    self.suite = val
                elif key == 'frames':
                    self.frames = int(val)
                elif key == 'watchdog':
                    self.watchdog = int(val)
                elif key == 'fixture':
                    lv, mission = val.split()
                    self.fixture = (lv, int(mission))
                elif key == 'resources':
                    self.resources = val
                elif key == 'checkpoint':
                    self.checkpoint = int(val)
                elif key == 'audio':
                    self.audio = val.lower() in ('yes', 'true', '1')
                elif key == 'golden':
                    self.golden = val.lower() in ('yes', 'true', '1')
                elif key == 'performance':
                    self.performance = val.lower() in ('yes', 'true', '1')
                elif key == 'expect':
                    self.expects.append((lineno, val))
                else:
                    raise ValueError('%s:%d: unknown key %r' % (path, lineno, key))
        if self.frames <= 0:
            raise ValueError('%s: frames must be set' % path)


def load_scenarios(only=None, names=None):
    out = []
    for fn in sorted(os.listdir(SCENARIOS)):
        if fn.endswith('.scn'):
            s = Scenario(os.path.join(SCENARIOS, fn))
            if names and s.name not in names:
                continue
            if only and only not in s.name:
                continue
            out.append(s)
    return out


def make_resources_view(res, rundir, kind):
    """A copy of the resource tree made of symlinks, with parts left out."""
    view = os.path.join(rundir, 'resources')
    os.makedirs(view)
    for entry in os.listdir(res):
        if kind == 'no-video' and entry.lower() == 'video':
            continue
        os.symlink(os.path.join(res, entry), os.path.join(view, entry))
    return view


def prepare_savedir(scn, savedir, mksave):
    os.makedirs(savedir)
    if not scn.fixture:
        return None
    base = os.path.join(FIXTURES, 'base.save')
    if not os.path.exists(base):
        return 'missing %s (run: tests/run.py make-fixtures)' % base
    lv, mission = scn.fixture
    rc, out, err = run([mksave, base, os.path.join(savedir, 'SF#A'), str(LEVELS[lv]), str(mission)])
    if rc != 0:
        return 'sf_mksave failed: ' + err.strip()
    return None


def run_scenario(scn, binary, res, variant, mksave, tag=''):
    """Runs one scenario; returns a dict with everything the checks need."""
    rundir = os.path.join(BUILD, 'runs', variant, scn.name + tag)
    shutil.rmtree(rundir, ignore_errors=True)
    os.makedirs(os.path.join(rundir, 'frames'))
    info = {'rundir': rundir, 'error': None}
    savedir = os.path.join(rundir, 'save')
    err = prepare_savedir(scn, savedir, mksave)
    if err:
        info['error'] = err
        return info
    if scn.resources:
        res = make_resources_view(res, rundir, scn.resources)
    script = os.path.join(rundir, 'script.txt')
    with open(script, 'w') as f:
        f.write('\n'.join(scn.script) + '\n')
    wd = scn.watchdog * (6 if variant == 'asan' else 1)
    env = clean_env({
        'SF_SCRIPT': script, 'SF_MAXFRAMES': str(scn.frames), 'SF_WATCHDOG': str(wd),
        'SF_HASH': os.path.join(rundir, 'frames.txt'), 'SF_STATE': os.path.join(rundir, 'state.tsv'),
        'SF_DUMP': os.path.join(rundir, 'frames'), 'SF_DUMP_EVERY': str(scn.checkpoint),
        'SF_SAVEDIR': savedir,
    })
    if scn.audio:
        env['SF_AUDIO_DUMP'] = os.path.join(rundir, 'audio.wav')
    t0 = time.time()
    try:
        rc, out, errtext = run([binary, res], env=env, timeout=wd + 60)
    except subprocess.TimeoutExpired:
        rc, out, errtext = None, '', 'killed after %d s' % (wd + 60)
    info.update(rc=rc, stdout=out, stderr=errtext, secs=time.time() - t0, savedir=savedir)
    with open(os.path.join(rundir, 'stderr.txt'), 'w') as f:
        f.write(errtext)
    with open(os.path.join(rundir, 'stdout.txt'), 'w') as f:
        f.write(out)
    return info


def read_state(path):
    rows = []
    try:
        with open(path) as f:
            header = f.readline().rstrip('\n').split('\t')
            for line in f:
                vals = line.rstrip('\n').split('\t')
                if len(vals) != len(header) or not all(v.lstrip('-').isdigit() for v in vals):
                    continue  # a line cut short by a crash
                rows.append({k: int(v) for k, v in zip(header, vals)})
    except OSError:
        pass
    return rows


def read_hashes(path):
    try:
        with open(path) as f:
            return [p[1] for p in (line.split() for line in f) if len(p) == 2]
    except OSError:
        return None


def wav_stats(path):
    import array
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError:
        return None
    pcm = array.array('h')
    pcm.frombytes(data[44:44 + ((len(data) - 44) // 2) * 2])
    if sys.byteorder != 'little':
        pcm.byteswap()
    peak = max((abs(v) for v in pcm), default=0)
    return {'sha256': hashlib.sha256(data).hexdigest(), 'seconds': len(pcm) / 2 / 44100.0,
            'peak': peak}


class ExpectEnv(dict):
    """Names available to 'expect:' expressions."""

    def __init__(self, rows, savedir, audio):
        super().__init__()
        byframe = {r['frame']: r for r in rows}

        def col(name, a=1, b=None):
            return [r[name] for r in rows if r['frame'] >= a and (b is None or r['frame'] <= b)]

        def at(frame):
            return byframe[frame]

        def first(name, value, a=1):
            for r in rows:
                if r['frame'] >= a and r[name] == value:
                    return r['frame']
            return None

        def ever(name, value, a=1, b=None):
            return value in col(name, a, b)

        def seq(name, a=1, b=None):
            out = []
            for v in col(name, a, b):
                if not out or out[-1] != v:
                    out.append(v)
            return out

        def contains_in_order(values, wanted):
            it = iter(values)
            return all(any(v == w for v in it) for w in wanted)

        def saved(fn):
            return os.path.exists(os.path.join(savedir, fn))

        def size(fn):
            p = os.path.join(savedir, fn)
            return os.path.getsize(p) if os.path.exists(p) else -1

        self.update(S=rows, last=rows[-1] if rows else {}, col=col, at=at, first=first, ever=ever,
                    seq=seq, in_order=contains_in_order, saved=saved, size=size, audio=audio or {},
                    max=max, min=min, any=any, all=all, len=len, sum=sum, abs=abs)


def golden_dir(scn):
    return os.path.join(GOLDEN, scn.name)


def read_meta(path):
    meta = {}
    try:
        with open(path) as f:
            for line in f:
                k, _, v = line.partition(':')
                meta[k.strip()] = v.strip()
    except OSError:
        pass
    return meta


def checkpoint_frames(rundir):
    d = os.path.join(rundir, 'frames')
    out = []
    for fn in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if fn.endswith('.ppm'):
            out.append((int(fn[1:7]), os.path.join(d, fn)))
    return out


def check_scenario(scn, info, variant, results, manifest, compare_golden=True):
    group = 'scenario/' + variant
    if info.get('error'):
        results.add(Result(group, scn.name, FAIL, info['error']))
        return
    rc, err, secs = info['rc'], info['stderr'], info['secs']
    # 1. ran to the frame limit without crashing or hanging
    if rc == 0 and 'max frames reached' in err:
        results.add(Result(group, scn.name + ': runs to frame %d' % scn.frames, PASS, '', secs))
    else:
        why = {None: 'killed (timeout)', 3: 'watchdog fired (hang)', 2: 'test hook error'}.get(
            rc, 'exit code %s' % rc)
        if rc is not None and rc < 0:
            why = 'crashed with signal %d' % -rc
        results.add(Result(group, scn.name + ': runs to frame %d' % scn.frames, FAIL,
                           '%s\n%s' % (why, err[-3000:]), secs))
    san = sanitizer_report(err)
    if san:
        results.add(Result(group, scn.name + ': sanitizer', FAIL, san))
    # 2. behavioural expectations on the state log
    rows = read_state(os.path.join(info['rundir'], 'state.tsv'))
    audio = wav_stats(os.path.join(info['rundir'], 'audio.wav')) if scn.audio else None
    info['audio'] = audio
    env = ExpectEnv(rows, info['savedir'], audio)
    for lineno, expr in scn.expects:
        try:
            ok = bool(eval(expr, {'__builtins__': {}}, env))
            results.add(Result(group, '%s: expect %s' % (scn.name, expr), PASS if ok else FAIL,
                               '' if ok else 'false (%s.scn line %d)' % (scn.name, lineno)))
        except Exception as e:  # a bad expression is a failed check
            results.add(Result(group, '%s: expect %s' % (scn.name, expr), FAIL,
                               '%s: %s (%s.scn line %d)' % (type(e).__name__, e, scn.name, lineno)))
    # 3. golden frames / audio
    if scn.golden and compare_golden:
        compare_with_golden(scn, info, variant, results, manifest, audio)
    # contact sheet of the run for the report
    frames = checkpoint_frames(info['rundir'])
    if frames:
        os.makedirs(os.path.join(REPORT, 'img'), exist_ok=True)
        sheet = os.path.join(REPORT, 'img', '%s-%s-sheet.png' % (variant, scn.name))
        contact_sheet([(f, read_ppm(p)[2]) for f, p in frames], sheet)
        info['sheet'] = sheet
    if audio and not KEEP:
        try:
            os.remove(os.path.join(info['rundir'], 'audio.wav'))
        except OSError:
            pass


def compare_with_golden(scn, info, variant, results, manifest, audio):
    group = 'scenario/' + variant
    gdir = golden_dir(scn)
    want = read_hashes(os.path.join(gdir, 'frames.txt'))
    got = read_hashes(os.path.join(info['rundir'], 'frames.txt')) or []
    meta = read_meta(os.path.join(gdir, 'meta.txt'))
    name = scn.name + ': frames match golden'
    if want is None:
        results.add(Result(group, name, FAIL, 'no golden yet - run: make rebless SCENARIOS=%s' % scn.name))
        return
    if meta.get('resources') and meta['resources'] != manifest:
        results.add(Result(group, name, WARN,
                           'goldens were recorded with different SF_Resources (%s, now %s): not compared; '
                           'run make rebless if the new resources are intended' % (meta['resources'], manifest)))
        return
    first = next((i for i, (a, b) in enumerate(zip(want, got)) if a != b), None)
    if first is None and len(want) == len(got):
        results.add(Result(group, name, PASS, '%d frames' % len(got)))
    else:
        if first is None:
            first = min(len(want), len(got))
        ndiff = sum(1 for a, b in zip(want, got) if a != b) + abs(len(want) - len(got))
        images = diff_images(scn, info, first + 1)
        hint = ''
        if meta.get('compiler') and meta['compiler'] != compiler_id():
            hint = ('\ngoldens were recorded with "%s", this build uses "%s"; if nothing else changed, '
                    'check the images and run make rebless' % (meta['compiler'], compiler_id()))
        results.add(Result(group, name, FAIL,
                           'first differs at frame %d; %d of %d frames differ%s'
                           % (first + 1, ndiff, max(len(want), len(got)), hint), images=images))
    if scn.audio:
        gaudio = read_meta(os.path.join(gdir, 'audio.txt'))
        aname = scn.name + ': audio matches golden'
        if not gaudio.get('sha256'):
            results.add(Result(group, aname, FAIL, 'no audio golden - run make rebless'))
        elif not audio:
            results.add(Result(group, aname, FAIL, 'no audio captured'))
        elif gaudio['sha256'] == audio['sha256']:
            results.add(Result(group, aname, PASS, '%.1f s' % audio['seconds']))
        else:
            results.add(Result(group, aname, FAIL, 'audio differs (%.1f s now, %s s in golden)'
                               % (audio['seconds'], gaudio.get('seconds', '?'))))


def diff_images(scn, info, first_frame):
    """expected / actual / diff PNGs for up to 3 checkpoints at or after the first difference."""
    out = []
    gck = os.path.join(golden_dir(scn), 'checkpoints')
    os.makedirs(os.path.join(REPORT, 'img'), exist_ok=True)
    for frame, ppm in checkpoint_frames(info['rundir']):
        if frame < first_frame - scn.checkpoint or len(out) >= 3:
            continue
        exp = os.path.join(gck, 'f%06d.png' % frame)
        if not os.path.exists(exp):
            continue
        w, h, actual = read_ppm(ppm)
        _, _, expected = read_png(exp)
        d, n = diff_image(expected, actual, w, h)
        if n == 0:
            continue
        base = os.path.join(REPORT, 'img', '%s-f%06d' % (scn.name, frame))
        write_png(base + '-actual.png', w, h, actual)
        write_png(base + '-diff.png', w, h, d)
        shutil.copy(exp, base + '-expected.png')
        out.append((frame, n, base))
    return out


def write_golden(scn, info, manifest):
    gdir = golden_dir(scn)
    old = read_hashes(os.path.join(gdir, 'frames.txt'))
    shutil.rmtree(gdir, ignore_errors=True)
    os.makedirs(os.path.join(gdir, 'checkpoints'))
    shutil.copy(os.path.join(info['rundir'], 'frames.txt'), os.path.join(gdir, 'frames.txt'))
    for frame, ppm in checkpoint_frames(info['rundir']):
        w, h, rgb = read_ppm(ppm)
        write_png(os.path.join(gdir, 'checkpoints', 'f%06d.png' % frame), w, h, rgb)
    if scn.audio:
        a = wav_stats(os.path.join(info['rundir'], 'audio.wav'))
        with open(os.path.join(gdir, 'audio.txt'), 'w') as f:
            f.write('sha256: %s\nseconds: %.2f\npeak: %d\n' % (a['sha256'], a['seconds'], a['peak']))
    with open(os.path.join(gdir, 'meta.txt'), 'w') as f:
        f.write('scenario: %s\nframes: %d\nresources: %s\ncompiler: %s\ncflags: %s\nrecorded: %s\n'
                % (scn.name, scn.frames, manifest, compiler_id(), TEST_CFLAGS,
                   time.strftime('%Y-%m-%d %H:%M:%S')))
    new = read_hashes(os.path.join(gdir, 'frames.txt'))
    if old is None:
        return 'new'
    return 'unchanged' if old == new else 'changed'


def run_scenarios(scns, binary, res, variant, mksave, jobs, results, manifest, compare_golden=True):
    def one(s):
        return s, run_scenario(s, binary, res, variant, mksave)

    infos = {}
    with cf.ThreadPoolExecutor(jobs) as ex:
        for s, info in ex.map(one, scns):
            check_scenario(s, info, variant, results, manifest, compare_golden)
            infos[s.name] = info
    return infos


def determinism_check(scn, binary, res, mksave, results, first_info, repeats=2):
    """Re-runs a scenario: frames, state log and audio must be identical every time.  Address
    randomisation stays on, so anything that depends on memory addresses or uninitialised memory
    shows up here (as the explosion smoke bug did)."""
    def digest(info):
        out = {}
        for fn in ('frames.txt', 'state.tsv'):
            try:
                with open(os.path.join(info['rundir'], fn), 'rb') as f:
                    out[fn] = hashlib.sha256(f.read()).hexdigest()
            except OSError:
                out[fn] = None
        a = info.get('audio') or wav_stats(os.path.join(info['rundir'], 'audio.wav'))
        out['audio'] = a['sha256'] if a else None
        return out

    ref = digest(first_info)
    diffs = set()
    for k in range(repeats):
        info = run_scenario(scn, binary, res, 'headless', mksave, tag='-again%d' % k)
        if info.get('error') or info.get('rc') != 0:
            results.add(Result('determinism', scn.name, FAIL, 'repeat run failed: %s'
                               % (info.get('error') or info.get('stderr', '')[-500:])))
            return
        d = digest(info)
        diffs.update(fn for fn in ref if ref[fn] != d[fn])
        if not KEEP:
            try:
                os.remove(os.path.join(info['rundir'], 'audio.wav'))
            except OSError:
                pass
    results.add(Result('determinism', '%s: %d runs identical' % (scn.name, repeats + 1),
                       FAIL if diffs else PASS, 'differs: ' + ', '.join(sorted(diffs)) if diffs else ''))


def sdl_smoke(binary, res, results):
    """The real SDL build with dummy video/audio drivers: boots, takes scripted input, starts a game."""
    rundir = os.path.join(BUILD, 'runs', 'sdl', 'smoke')
    shutil.rmtree(rundir, ignore_errors=True)
    os.makedirs(rundir)
    script = os.path.join(rundir, 'script.txt')
    # skip the videos and the logo, start the first mission, take off
    with open(script, 'w') as f:
        f.write('20 400 DOWN\n420 424 A\n500 504 A\n600 604 A\n700 704 A\n800 804 A\n900 904 A\n')
    frames = 1100
    env = clean_env({'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'SF_SCRIPT': script,
                     'SF_MAXFRAMES': str(frames), 'SF_WATCHDOG': '240',
                     'SF_STATE': os.path.join(rundir, 'state.tsv'),
                     'SF_HASH': os.path.join(rundir, 'frames.txt'),
                     'SF_SAVEDIR': os.path.join(rundir, 'save')})
    t0 = time.time()
    try:  # run in its own folder: the game writes config.txt into the current directory
        rc, out, err = run([binary, res], cwd=rundir, env=env, timeout=300)
    except subprocess.TimeoutExpired:
        rc, out, err = None, '', 'timed out'
    secs = time.time() - t0
    rows = read_state(os.path.join(rundir, 'state.tsv'))
    keys_file_check(binary, res, rundir, err, results)
    if rc != 0 or 'max frames reached' not in err:
        results.add(Result('sdl', 'runs with dummy drivers to frame %d' % frames, FAIL,
                           'exit %s\n%s' % (rc, err[-2000:]), secs))
        return
    results.add(Result('sdl', 'runs with dummy drivers to frame %d' % frames, PASS, '', secs))
    menus = set(r['menu'] for r in rows)
    flying = any(r['status'] == 1 and r['menu'] == -1 for r in rows)
    results.add(Result('sdl', 'scripted input reaches the main menu', PASS if 0 in menus else FAIL,
                       'menus seen: %s' % sorted(menus)))
    results.add(Result('sdl', 'scripted input starts a mission', PASS if flying else FAIL,
                       '' if flying else 'never in flight (status 1); menus seen: %s' % sorted(menus)))


def keys_file_check(binary, res, rundir, first_err, results):
    """config.txt: written with the defaults on the first run; a valid edit is loaded; invalid keys
    or a key used for two controls are all reported and the game exits."""
    path = os.path.join(rundir, 'config.txt')
    try:
        with open(path) as f:
            text = f.read()
    except OSError:
        text = ''
    ok = 'keys: wrote' in first_err and all(
        re.search(r'(?m)^%s\s*=' % c, text) for c in ('UP', 'DOWN', 'A', 'START', 'X', 'LS', 'RS'))
    results.add(Result('sdl', 'writes the default keyboard layout to config.txt', PASS if ok else FAIL,
                       '' if ok else 'stderr: %s\nconfig.txt:\n%s' % (first_err[-500:], text[:800])))
    env = clean_env({'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'SF_MAXFRAMES': '5',
                     'SF_SAVEDIR': os.path.join(rundir, 'save')})

    def run_with(text):
        with open(path, 'w') as f:
            f.write(text)
        try:
            return run([binary, res], cwd=rundir, env=env, timeout=120)[::2]
        except subprocess.TimeoutExpired:
            return None, 'timed out'

    rc, err = run_with('# edited\nSTART = Return\nLS = Q, W\n')
    ok = rc == 0 and 'keys: loaded config.txt' in err
    results.add(Result('sdl', 'reads a valid edited config.txt', PASS if ok else FAIL,
                       '' if ok else 'exit %s\n%s' % (rc, err[-1000:])))
    rc, err = run_with('# edited\nSTART = A\nfire = G\nRS = NoSuchKey\n')
    wanted = ['config.txt:3: unknown control', 'config.txt:4: unknown key name',
              "key 'A' is used for both START (line 2) and LS", 'config.txt has 3 problems']
    ok = rc == 1 and all(w in err for w in wanted) and 'max frames reached' not in err
    results.add(Result('sdl', 'refuses a config.txt with invalid or shared keys', PASS if ok else FAIL,
                       '' if ok else 'exit %s (expected 1)\nmissing: %s\n%s'
                       % (rc, [w for w in wanted if w not in err], err[-1000:])))


# --------------------------------------------------------------------------------------------------
# report
def write_report(results, title, started, infos):
    os.makedirs(REPORT, exist_ok=True)
    counts = {s: results.count(s) for s in (PASS, FAIL, WARN, SKIP)}
    with open(os.path.join(REPORT, 'summary.txt'), 'w') as f:
        f.write('%s: %d passed, %d failed, %d warnings, %d skipped (%.0f s)\n'
                % (title, counts[PASS], counts[FAIL], counts[WARN], counts[SKIP], time.time() - started))
        for r in results.items:
            f.write('%-4s %-16s %s\n' % (r.status, r.group, r.name))
            if r.status != PASS and r.detail:
                f.write(''.join('       %s\n' % l for l in r.detail.splitlines()))
    colour = {PASS: '#2e7d32', FAIL: '#c62828', WARN: '#ef6c00', SKIP: '#757575'}
    rows = []
    for r in sorted(results.items, key=lambda r: ({FAIL: 0, WARN: 1, SKIP: 2, PASS: 3}[r.status])):
        imgs = ''
        for frame, n, base in r.images:
            rel = os.path.relpath(base, REPORT)
            imgs += ('<div class=cmp><b>frame %d</b> (%d pixels differ)<br>'
                     '<figure><img src="%s-expected.png"><figcaption>expected</figcaption></figure>'
                     '<figure><img src="%s-actual.png"><figcaption>actual</figcaption></figure>'
                     '<figure><img src="%s-diff.png"><figcaption>difference</figcaption></figure></div>'
                     % (frame, n, rel, rel, rel))
        rows.append('<tr><td style="color:%s"><b>%s</b></td><td>%s</td><td>%s</td><td>%s</td>'
                    '<td><pre>%s</pre>%s</td></tr>'
                    % (colour[r.status], r.status, html.escape(r.group), html.escape(r.name),
                       '%.1fs' % r.secs if r.secs else '', html.escape(r.detail[:4000]), imgs))
    sheets = ''
    for name, info in sorted(infos.items()):
        if info.get('sheet'):
            sheets += ('<h3>%s</h3><img class=sheet src="%s">'
                       % (html.escape(name), os.path.relpath(info['sheet'], REPORT)))
    with open(os.path.join(REPORT, 'index.html'), 'w') as f:
        f.write('<!DOCTYPE html><html><head><meta charset="utf-8"><title>%s</title><style>'
                'body{font-family:sans-serif;margin:20px}table{border-collapse:collapse;width:100%%}'
                'td{border-bottom:1px solid #ddd;padding:4px;vertical-align:top;font-size:13px}'
                'pre{margin:0;white-space:pre-wrap}figure{display:inline-block;margin:4px}'
                'figure img{width:320px;image-rendering:pixelated}.sheet{max-width:100%%}'
                '</style></head><body><h1>%s</h1><p>%d passed, %d failed, %d warnings, %d skipped. %s</p>'
                '<table>%s</table><h2>Scenario contact sheets (checkpoint frames)</h2>%s</body></html>'
                % (html.escape(title), html.escape(title), counts[PASS], counts[FAIL], counts[WARN],
                   counts[SKIP], time.strftime('%Y-%m-%d %H:%M'), '\n'.join(rows), sheets))


def known_issues(results):
    try:
        with open(UBSAN_SUPP) as f:
            for line in f:
                if line.strip().startswith('# KNOWN:'):
                    results.add(Result('known issue', line.split(':', 1)[1].strip(), WARN,
                                       'suppressed in tests/ubsan.supp'))
    except OSError:
        pass


# --------------------------------------------------------------------------------------------------
# commands
def find_resources(arg):
    res = arg or os.environ.get('SF_RESOURCES') or os.path.join(ROOT, 'SF_Resources')
    res = os.path.abspath(res)
    if not os.path.isdir(os.path.join(res, 'Missions')):
        sys.exit('tests/run.py: SF_Resources not found at %s (use --resources DIR or RES=DIR)' % res)
    return res


def cmd_suite(suite, args):
    started = time.time()
    res = find_resources(args.resources)
    jobs = args.jobs
    results = Results()
    infos = {}
    print('== Star Fighter tests: %s  (resources %s, %d jobs)' % (suite, res, jobs), flush=True)
    manifest, nfiles = resources_manifest(res)
    print('   resources manifest %s (%d files)' % (manifest, nfiles), flush=True)
    scns = load_scenarios(args.only)
    quick = [s for s in scns if s.suite == 'quick']
    if suite in ('quick', 'full'):
        print('-- build checks', flush=True)
        lint_checks(results)
        headless = build('headless', results, jobs, unit=True)
        sdl = None
        if sdl_available():
            sdl = build('sdl', results, jobs)
        else:
            results.add(Result('build', 'sdl', SKIP, 'SDL2 development files not found'))
        mingw_build(results, jobs)
        if which('clang'):
            build('clang', results, jobs, unit=True)
        else:
            results.add(Result('build', 'clang', SKIP, 'clang not installed'))
        if headless:
            mksave = os.path.join(BUILD, 'headless', 'tools', 'sf_mksave')
            print('-- unit tests', flush=True)
            run_unit_tests('headless', results, jobs, res, args.only)
            torun = quick if suite == 'quick' else scns
            print('-- scenarios (%d)' % len(torun), flush=True)
            infos.update(run_scenarios(torun, headless, res, 'headless', mksave, jobs, results, manifest))
            det = [s for s in torun if s.name in ('new-game-takeoff', 'crash-and-game-over')]
            if det:
                print('-- determinism', flush=True)
            for s in det:
                if s.name in infos:
                    determinism_check(s, headless, res, mksave, results, infos[s.name])
        if sdl:
            print('-- SDL build smoke test', flush=True)
            sdl_smoke(sdl, res, results)
    if suite in ('asan', 'full'):
        print('-- AddressSanitizer + UBSan build', flush=True)
        asan = build('asan', results, jobs, unit=True)
        if asan:
            mksave = os.path.join(BUILD, 'asan', 'tools', 'sf_mksave')
            run_unit_tests('asan', results, jobs, res, args.only)
            torun = quick if suite == 'asan' else scns
            print('-- scenarios under ASan (%d)' % len(torun), flush=True)
            ainfos = run_scenarios(torun, asan, res, 'asan', mksave, jobs, results, manifest)
            infos.update({'asan/' + k: v for k, v in ainfos.items()})
        known_issues(results)
    title = 'Star Fighter tests (%s)' % suite
    write_report(results, title, started, infos)
    print('\n%d passed, %d failed, %d warnings, %d skipped in %.0f s'
          % (results.count(PASS), results.count(FAIL), results.count(WARN), results.count(SKIP),
             time.time() - started))
    print('report: %s' % os.path.join(REPORT, 'index.html'))
    fails = [r for r in results.items if r.status == FAIL]
    if fails:
        print('\nFAILURES:')
        for r in fails:
            print('  %s %s' % (r.group, r.name))
            for l in r.detail.strip().splitlines()[:6]:
                print('      ' + l)
    return 1 if fails else 0


def cmd_rebless(args):
    res = find_resources(args.resources)
    results = Results()
    headless = build('headless', results, args.jobs, unit=True)
    if not headless:
        return 1
    mksave = os.path.join(BUILD, 'headless', 'tools', 'sf_mksave')
    manifest, _ = resources_manifest(res)
    scns = load_scenarios(args.only, set(args.names) if args.names else None)
    if args.names and len(scns) != len(set(args.names)):
        missing = set(args.names) - set(s.name for s in scns)
        print('unknown scenario(s): %s' % ', '.join(sorted(missing)))
        return 1
    scns = [s for s in scns if s.golden]

    def one(s):
        return s, run_scenario(s, headless, res, 'headless', mksave)

    status = 0
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        for s, info in ex.map(one, scns):
            if info.get('error') or info.get('rc') != 0:
                print('  %-24s NOT recorded: %s' % (s.name, info.get('error') or
                                                     'exit %s\n%s' % (info.get('rc'), info.get('stderr', '')[-1500:])))
                status = 1
                continue
            what = write_golden(s, info, manifest)
            print('  %-24s %s' % (s.name, what), flush=True)
            if not KEEP:
                try:
                    os.remove(os.path.join(info['rundir'], 'audio.wav'))
                except OSError:
                    pass
    print('goldens are in tests/golden/; review the checkpoint images before committing them')
    return status


def cmd_make_fixtures(args):
    """Plays tests/scenarios/save-and-load.scn and keeps the save file it writes."""
    res = find_resources(args.resources)
    results = Results()
    headless = build('headless', results, args.jobs, unit=True)
    if not headless:
        return 1
    scn = Scenario(os.path.join(SCENARIOS, 'save-and-load.scn'))
    info = run_scenario(scn, headless, res, 'headless', None)
    src = os.path.join(info['rundir'], 'save', 'SF#A')
    if info.get('rc') != 0 or not os.path.exists(src):
        print('the save scenario did not write a save file:\n%s' % info.get('stderr', '')[-2000:])
        return 1
    os.makedirs(FIXTURES, exist_ok=True)
    shutil.copy(src, os.path.join(FIXTURES, 'base.save'))
    print('wrote tests/fixtures/base.save (%d bytes)' % os.path.getsize(src))
    return 0


def perf_run(scn, binary, res, mksave):
    """One timed run: only the script, frame limit and save directory are set, so no frame dumps,
    hashes, state log or audio capture add to the time.  Returns (wall s, cpu s) or an error."""
    import resource
    rundir = os.path.join(BUILD, 'runs', 'perf', scn.name)
    shutil.rmtree(rundir, ignore_errors=True)
    os.makedirs(rundir)
    savedir = os.path.join(rundir, 'save')
    err = prepare_savedir(scn, savedir, mksave)
    if err:
        return None, err
    if scn.resources:
        res = make_resources_view(res, rundir, scn.resources)
    script = os.path.join(rundir, 'script.txt')
    with open(script, 'w') as f:
        f.write('\n'.join(scn.script) + '\n')
    env = clean_env({'SF_SCRIPT': script, 'SF_MAXFRAMES': str(scn.frames), 'SF_SAVEDIR': savedir,
                     'SF_WATCHDOG': str(scn.watchdog)})
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.perf_counter()
    try:
        rc, out, errtext = run([binary, res], env=env, timeout=scn.watchdog + 60)
    except subprocess.TimeoutExpired:
        return None, 'timed out'
    wall = time.perf_counter() - t0
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if rc != 0 or 'max frames reached' not in errtext:
        return None, 'exit %s: %s' % (rc, errtext[-300:])
    cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    return (wall, cpu), None


def cmd_performance(args):
    """Frames per second of the headless build (the same optimised build the tests use), for the
    scenarios marked 'performance: yes' - those that fly in the 3D game engine.  The
    headless build never sleeps (its clock is virtual), so this is how fast the engine can produce
    frames.  Scenarios run one at a time; each is repeated and the median and best are shown.
    Wall-clock time depends on the machine's load, CPU fps (frames / CPU time used) less so."""
    import statistics
    res = find_resources(args.resources)
    results = Results()
    binary = build('headless', results, args.jobs, unit=True)
    if not binary:
        return 1
    mksave = os.path.join(BUILD, 'headless', 'tools', 'sf_mksave')
    scns = [s for s in load_scenarios(args.only) if s.performance]
    if not scns:
        print('no scenarios marked "performance: yes"%s' % (' matching ' + args.only if args.only else ''))
        return 1
    repeat = max(1, args.repeat)
    print('-- performance: %d scenarios x %d runs, one at a time' % (len(scns), repeat), flush=True)
    rows, failed = [], 0
    hdr = '%-22s %7s %10s %10s %10s %7s' % ('scenario', 'frames', 'fps(med)', 'fps(best)', 'cpu fps',
                                            'spread')
    print(hdr)
    for scn in scns:
        walls, cpus, err = [], [], None
        for _ in range(repeat):
            t, err = perf_run(scn, binary, res, mksave)
            if t is None:
                break
            walls.append(t[0])
            cpus.append(t[1])
        if err:
            print('%-22s FAILED: %s' % (scn.name, err.strip().splitlines()[0] if err.strip() else err))
            failed += 1
            continue
        med, best, cmed = statistics.median(walls), min(walls), statistics.median(cpus)
        row = dict(scenario=scn.name, frames=scn.frames, fps_median=scn.frames / med,
                   fps_best=scn.frames / best, cpu_fps=scn.frames / cmed if cmed else 0.0,
                   spread=100.0 * (max(walls) - best) / best, wall_median=med, cpu_median=cmed)
        rows.append(row)
        print('%-22s %7d %10.0f %10.0f %10.0f %6.0f%%' % (scn.name, scn.frames, row['fps_median'],
                                                         row['fps_best'], row['cpu_fps'], row['spread']),
              flush=True)
    if rows:
        frames = sum(r['frames'] for r in rows)
        total = dict(scenario='TOTAL', frames=frames,
                     fps_median=frames / sum(r['wall_median'] for r in rows),
                     fps_best=frames / sum(r['frames'] / r['fps_best'] for r in rows),
                     cpu_fps=frames / sum(r['cpu_median'] for r in rows), spread=0.0)
        print('%-22s %7d %10.0f %10.0f %10.0f' % ('TOTAL', frames, total['fps_median'], total['fps_best'],
                                                 total['cpu_fps']))
        rows.append(total)
        stamp = time.strftime('%Y-%m-%d %H:%M:%S')
        os.makedirs(REPORT, exist_ok=True)
        cols = ['scenario', 'frames', 'fps_median', 'fps_best', 'cpu_fps', 'spread']
        with open(os.path.join(REPORT, 'performance.csv'), 'w') as f:
            f.write(','.join(cols) + '\n')
            for r in rows:
                f.write(','.join(str(round(r[c], 1)) if isinstance(r[c], float) else str(r[c])
                                 for c in cols) + '\n')
        hist = os.path.join(BUILD, 'performance-history.csv')
        new = not os.path.exists(hist)
        with open(hist, 'a') as f:
            if new:
                f.write('time,compiler,repeat,' + ','.join(cols) + '\n')
            for r in rows:
                f.write('%s,"%s",%d,' % (stamp, compiler_id(), repeat) +
                        ','.join(str(round(r[c], 1)) if isinstance(r[c], float) else str(r[c])
                                 for c in cols) + '\n')
        print('\nOnly scenarios that fly in the 3D game engine are measured ("performance: yes").')
        print('fps = frames / time for the whole scenario, including the menus before the mission starts.')
        print('Wall-clock fps depends on machine load; compare the best or CPU figures between runs.')
        print('written: %s, appended to %s' % (os.path.join(REPORT, 'performance.csv'), hist))
    return 1 if failed else 0


def main():
    global KEEP
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=['quick', 'asan', 'full', 'rebless', 'make-fixtures', 'list',
                                        'performance'])
    ap.add_argument('names', nargs='*', help='scenarios (rebless only)')
    ap.add_argument('--resources')
    ap.add_argument('--only')
    ap.add_argument('-j', '--jobs', type=int, default=ncpu())
    ap.add_argument('--keep', action='store_true')
    ap.add_argument('--repeat', type=int, default=3)
    args = ap.parse_args()
    KEEP = args.keep
    if args.command == 'list':
        for s in load_scenarios():
            print('%-24s %-6s %6d frames  %s' % (s.name, s.suite, s.frames, s.description))
        return 0
    if args.command == 'rebless':
        return cmd_rebless(args)
    if args.command == 'make-fixtures':
        return cmd_make_fixtures(args)
    if args.command == 'performance':
        return cmd_performance(args)
    return cmd_suite(args.command, args)


KEEP = False
if __name__ == '__main__':
    sys.exit(main())
