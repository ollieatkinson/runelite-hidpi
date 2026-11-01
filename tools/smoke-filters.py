#!/usr/bin/env python3
"""Run synthetic window tests in a private, headless KWin session.

Pass paths to Xwayland, nested-wm, the compiled smoke-window client, and a log
directory. No running desktop or RuneLite profile is changed.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

if '--private-bus' not in sys.argv:
    os.execvp('dbus-run-session', ['dbus-run-session', '--', sys.executable,
                                  str(Path(__file__).resolve()), '--private-bus', *sys.argv[1:]])
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--private-bus', action='store_true', help=argparse.SUPPRESS)
p.add_argument('--scale', type=float, default=1.25)
p.add_argument('--move-scale', type=float, help='Move to a second output at this scale and back')
p.add_argument('--gpu', action='store_true', help='Require the GPU bicubic path')
p.add_argument('xwayland', type=Path)
p.add_argument('wm', type=Path)
p.add_argument('client', type=Path)
p.add_argument('logs', type=Path)
a = p.parse_args()
a.logs = a.logs.resolve()
a.logs.mkdir(parents=True, exist_ok=True)
processes, files = [], []

with tempfile.TemporaryDirectory(prefix='runelite-filter-smoke-') as directory:
    base = Path(directory)
    runtime = base / 'runtime'
    runtime.mkdir(mode=0o700)
    env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(base/'config'),
               XDG_DATA_HOME=str(base/'data'), XDG_CACHE_HOME=str(base/'cache'),
               QT_QPA_PLATFORM='offscreen', WAYLAND_DISPLAY='filter-test')

    def start(command, name, environment=env):
        log = open(a.logs / (name + '.log'), 'w')
        files.append(log)
        process = subprocess.Popen(command, env=environment, stdout=log, stderr=log)
        processes.append(process)
        return process

    def compositor(name):
        process = start(['kwin_wayland', '--virtual', '--width', '1280', '--height', '800',
                         '--socket', 'filter-test', '--no-lockscreen', '--no-global-shortcuts',
                         '--no-kactivities', '--output-count', '2' if a.move_scale else '1'], name)
        for _ in range(100):
            if (runtime/'filter-test').exists():
                return process
            assert process.poll() is None, 'Headless KWin exited'
            time.sleep(.1)
        raise RuntimeError('Headless KWin never became ready')

    def move(server, x):
        script = base/'move.js'
        script.write_text(f'for(const w of workspace.windowList())if(w.pid==={server.pid})'
                          f'{{const g=w.frameGeometry;w.frameGeometry={{x:{x},y:20,width:g.width,height:g.height}};}}')
        def dbus(*arguments):
            return subprocess.check_output(['qdbus6', 'org.kde.KWin', *arguments], env=env, timeout=5, text=True).strip()
        identifier = dbus('/Scripting', 'org.kde.kwin.Scripting.loadScript', str(script), 'filter-move')
        dbus('/Scripting/Script'+identifier, 'org.kde.kwin.Script.run')
        dbus('/Scripting', 'org.kde.kwin.Scripting.unloadScript', 'filter-move')

    try:
        # Virtual outputs load saved scale settings rather than the --scale flag.
        initial = compositor('kwin-initial')
        config = base/'config/kwinoutputconfig.json'
        for _ in range(100):
            if config.exists():
                break
            time.sleep(.1)
        initial.terminate()
        initial.wait(timeout=5)
        settings = json.loads(config.read_text())
        for entry in settings:
            if entry['name'] == 'outputs':
                for index, output in enumerate(entry['data']):
                    output['scale'] = a.scale if index == 0 else a.move_scale
                mode = entry['data'][0]['mode']
                # KWin 6.8 stores modes as basic/CVT variants.
                mode_width = mode.get('width') or mode.get('basic', {}).get('width')
                if mode_width is None:
                    mode_width = mode['cvt']['hdisplay']
                second_x = round(mode_width / a.scale)
            if entry['name'] == 'setups' and a.move_scale:
                for setup in entry['data']:
                    setup['outputs'][1]['position']['x'] = second_x
        config.write_text(json.dumps(settings))
        compositor('kwin')
        for number, method in enumerate(['compositor', 'bicubic'], 80):
            if Path(f'/tmp/.X11-unix/X{number}').exists() or Path(f'/tmp/.X{number}-lock').exists():
                raise RuntimeError(f'Test display :{number} is occupied')
            client_env = dict(env, DISPLAY=f':{number}', WAYLAND_DEBUG='1')
            client_env.pop('RUNELITE_HIDPI_BICUBIC', None)
            client_env.pop('RUNELITE_HIDPI_BICUBIC_GPU', None)
            if method == 'bicubic':
                client_env['RUNELITE_HIDPI_BICUBIC'] = '1'
                if a.gpu:
                    client_env['RUNELITE_HIDPI_BICUBIC_GPU'] = '1'
            server = start([str(a.xwayland.resolve()), client_env['DISPLAY'], '-geometry', '800x500',
                            '-hidpi', '-nolisten', 'tcp'], method, client_env)
            for _ in range(100):
                assert server.poll() is None, method + ' server exited'
                if subprocess.run(['xdpyinfo'], env=client_env, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, timeout=2).returncode == 0:
                    break
                time.sleep(.1)
            wm = start([str(a.wm.resolve())], method+'-wm', client_env)
            time.sleep(.6)
            client = start([str(a.client.resolve()), '800' if a.move_scale else '120'], method+'-client', client_env)
            if a.move_scale and method == 'bicubic':
                time.sleep(2)
                move(server, second_x + 10)
                time.sleep(1.5)
                move(server, 10)
            assert client.wait(timeout=15) == 0, method + ' client checks failed'
            time.sleep(.2)
            assert server.poll() is None, method + ' server crashed'
            wm.terminate(); wm.wait(timeout=5)
            server.terminate(); server.wait(timeout=5)
            log = (a.logs/(method+'.log')).read_text()
            if method != 'compositor':
                assert f'preferred_scale({round(a.scale * 120)})' in log, 'Requested output scale was not established'
                if a.gpu:
                    marker = 'RuneLite filter bicubic GPU:'
                    assert marker in log, 'GPU bicubic did not activate'
                    assert 'GPU filter unavailable' not in log, 'GPU filtering failed'
                    # Initial surface configuration can precede GPU readiness.
                    assert 'RuneLite bicubic: CPU' not in log[log.rindex(marker):], 'CPU fallback after GPU presentation'
                else:
                    assert 'RuneLite bicubic: CPU, using monitor preferred scale' in log, 'Bicubic did not activate'
                backing = 2  # Explicitly matches the launcher's Java render scale.
                width, height = round(800*a.scale/backing), round(400*a.scale/backing)
                expected = f'-> {width}x{height}' if a.gpu else f'{width}, {height}, {width*4}'
                assert expected in log, 'Expected scaled output buffer'
                if a.move_scale:
                    first, second = f'preferred_scale({round(a.scale*120)})', f'preferred_scale({round(a.move_scale*120)})'
                    assert second in log and first in log[log.index(second)+len(second):], 'Output move and return did not change scale'
                    width, height = round(800*a.move_scale/backing), round(400*a.move_scale/backing)
                    expected = f'-> {width}x{height}' if a.gpu else f'{width}, {height}, {width*4}'
                    assert expected in log, 'Second output buffer did not follow its scale'
                assert 'wl_buffer' in log and '.release()' in log, method + ' did not release buffers'
            assert 'protocol error' not in log, method + ' protocol error'
            print(f'{method} at {a.scale:g}x: PASS', flush=True)
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
        for process in processes:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
        for log in files:
            log.close()
