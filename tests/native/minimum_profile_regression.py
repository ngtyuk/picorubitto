#!/usr/bin/env python3
"""Exercise the real suite on native mruby without browser-only language gems."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
picoruby = Path(os.environ.get('PICORUBY_ROOT', root / 'deps/picoruby'))
mruby = picoruby / 'mrbgems/picoruby-mruby/lib/mruby'
output = root / 'build/tests/minimum-profile'
output.mkdir(parents=True, exist_ok=True)
# Rake writes a lock beside its config; keep that generated file under build/.
configuration = output / 'minimum_profile.rb'
configuration_source = (root / 'tests/native/minimum_profile.rb').read_text()
if not configuration.exists() or configuration.read_text() != configuration_source:
    configuration.write_text(configuration_source)
environment = dict(os.environ, MRUBY_CONFIG=str(configuration),
                   MRUBY_BUILD_DIR=str(output))
log = output / 'build.log'
with log.open('w') as stream:
    subprocess.run(['ruby', '-S', 'rake', '-f', str(mruby / 'Rakefile')],
                   cwd=root, env=environment, stdout=stream, stderr=subprocess.STDOUT, check=True)
fixture = (root / 'tests/native/minimum_checks.rb').read_text()
suite = (root / 'tests/portable/hardware_suite.rb').read_text()
flight = subprocess.check_output(['ruby', str(root / 'tools/build/checks_source.rb')], text=True)
performance = (root / 'tests/portable/hardware_performance.rb').read_text()
audio = (root / 'tests/portable/hardware_audio.rb').read_text()
vm_performance = (root / 'tests/portable/hardware_vm_performance.rb').read_text()
legacy = (root / 'tests/portable/hardware_performance_legacy.rb').read_text()
with tempfile.TemporaryDirectory(prefix='gaming-minimum-') as directory:
    for profile, rounds in [('standard', 42), ('comparison', 48)]:
        bundled = f'HARDWARE_PERFORMANCE_PROFILE = "{profile}"\n' + flight + performance + vm_performance + audio
        if profile == 'comparison':
            bundled += legacy
        bundled += suite
        source = Path(directory) / f'checks-{profile}.rb'
        source.write_text(fixture.replace('# SUITE_INSERTION_POINT', bundled))
        compiler = os.environ.get('CHECKS_MRBC', str(output / 'mrbc/default/bin/mrbc'))
        rom = Path(directory) / f'checks-{profile}.mrb'
        subprocess.run([compiler, '-o', str(rom), str(source)], check=True)
        run = subprocess.run([str(output / 'minimum-profile/bin/mruby'), '-b', str(rom)],
                             capture_output=True, text=True, timeout=30)
        print(run.stdout, end='')
        assert run.returncode == 0, run.stderr
        for title in ['RUBY / VM', 'COMMON API', 'BUTTONS', 'SAVE', 'ASSET / GC']:
            assert f'[CHECK] PASS {title}\n' in run.stdout
        for title in ['2D / TEXTURE', '3D / RESOLUTION', 'AUDIO', 'AUDIO / MIX', 'AUDIO / DIAL', 'AUDIO / RATE', 'FONT / CLIP']:
            assert f'[CHECK] OK {title} (user confirmation)\n' in run.stdout
        for title in ['PERF / 2D', 'PERF / 3D', 'PERF / RENDER', 'PERF / VM', 'PERF / TRI']:
            assert f'[CHECK] MEASURED {title}\n' in run.stdout
        assert run.stdout.count('frame_ms_mean=') == rounds
        assert run.stdout.count('work_ms_mean=') == 9
        assert f'profile={profile}' in run.stdout
        assert run.stdout.count('scene=3D-160 round=') == (9 if profile == 'comparison' else 0)
        assert run.stdout.count('scene=3D-320 round=') == (9 if profile == 'comparison' else 0)
        assert run.stdout.count('scene=3D-M160 round=') == 9
        assert run.stdout.count('scene=3D-M320 round=') == 9
        for scene in ['TRI-SM', 'TRI-SL', 'TRI-L160', 'TRI-L320']:
            assert run.stdout.count(f'scene={scene} round=') == 9
        assert run.stdout.count('[CHECK] FAIL') == 1
        assert '[CHECK] FAIL RUBY / VM: ArgumentError: expected smoke failure\n' in run.stdout
        assert '[MINIMUM] menu ready\n[MINIMUM] PASS\n' in run.stdout
        print(f'[MINIMUM] {profile}: PASS 17 checks / {rounds} rendering + 9 VM rounds')
