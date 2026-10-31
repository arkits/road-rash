#!/usr/bin/env python3
"""Receipted original-game frontend runs; preserves shared DLL/configuration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time
import uuid
from agent_common import ROOT, ResourceClaims, atomic_json, sha256
from oracle_capture import reference_hash


def capture(args):
    prefix = args.prefix.resolve()
    install = prefix / 'drive_c/ElectronicArts/RoadRashOracle'
    dll = install / 'AWEMAN32.DLL'
    ini = install / 'ddraw.ini'
    exe = install / 'ROADRASH.EXE'
    if sha256(exe) != reference_hash():
        raise ValueError('reference EXE hash mismatch')
    script = args.script.read_bytes()
    labels = re.findall(rb'^capture ([A-Za-z0-9_-]+)\s*$', script, re.M)
    if not labels or b'exit' not in script:
        raise ValueError('script requires captures and explicit exit')
    run = args.output.resolve() / uuid.uuid4().hex
    run.mkdir(parents=True)
    tag = 'fe-' + run.name
    croot = prefix / 'drive_c'
    script_path = croot / (tag + '.script')
    trace_path = croot / (tag + '.log')
    with ResourceClaims().hold('frontend-acceptance', [prefix, args.output]):
        saved_dll, saved_ini = dll.read_bytes(), ini.read_bytes()
        proc = None
        try:
            script_path.write_bytes(script)
            shutil.copy2(ROOT / 'oracle/build/AWEMAN32.DLL', dll)
            text = saved_ini.decode()
            for key, value in [('width','640'),('height','480'),('renderer','gdi'),('devmode','true')]:
                text = re.sub(r'(?m)^'+key+r'=[^\r\n]*',key+'='+value,text)
            ini.write_bytes(text.encode())
            env = {k:v for k,v in os.environ.items() if not k.startswith('RR_ORACLE_')}
            env.update(WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEDLLOVERRIDES='ddraw=n,b;aweman32=n',
                       RR_ORACLE_FRONTEND='1', RR_ORACLE_DRIVE=str(args.drive), RR_ORACLE_FRONTEND_SCRIPT='C:\\'+script_path.name,
                       RR_ORACLE_OUT='C:\\'+trace_path.name, RR_ORACLE_SCENARIO=tag)
            started=time.time()
            with (run/'wine.log').open('w') as out:
                proc=subprocess.Popen([str(args.wine),str(exe)],cwd=install,env=env,stdout=out,
                                      stderr=subprocess.STDOUT,start_new_session=True)
                try:
                    code=proc.wait(timeout=args.timeout)
                except (subprocess.TimeoutExpired,KeyboardInterrupt):
                    os.killpg(proc.pid,signal.SIGTERM)
                    try: proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(proc.pid,signal.SIGKILL);proc.wait()
                    raise RuntimeError('frontend capture interrupted/timed out')
            trace=trace_path.read_text(errors='replace')
            (run/'trace.log').write_text(trace)
            if code or '# fe complete' not in trace or '# fe FAIL' in trace:
                raise RuntimeError('original frontend did not complete successfully')
            artifacts=[]
            for path in croot.glob('fe-*.RRS'):
                if path.stat().st_mtime >= started:
                    dest=run/path.name;shutil.copy2(path,dest);artifacts.append(dest)
            for rawlabel in labels:
                label=rawlabel.decode()
                if '# fe captured '+label+' 1 screen ' not in trace:
                    raise RuntimeError('capture missing/invalid: '+label)
                for path in croot.glob('fe-'+label+'.*'):
                    if path.stat().st_mtime < started: continue
                    dest=run/path.name;shutil.copy2(path,dest);artifacts.append(dest)
                for path in croot.glob('fe-'+label+'-cache*.raw'):
                    if path.stat().st_mtime < started: continue
                    dest=run/path.name;shutil.copy2(path,dest);artifacts.append(dest)
            atomic_json(run/'receipt.json',dict(exe_sha256=sha256(exe),proxy_sha256=sha256(dll),
                script_sha256=hashlib.sha256(script).hexdigest(),script=script.decode(),
                exit_code=code,duration_seconds=time.time()-started,
                artifacts={p.name:sha256(p) for p in artifacts},complete=True))
            print(run)
        finally:
            dll.write_bytes(saved_dll);ini.write_bytes(saved_ini)
            if dll.read_bytes()!=saved_dll or ini.read_bytes()!=saved_ini:
                raise RuntimeError('original configuration restoration failed')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('script',type=Path)
    p.add_argument('--prefix',type=Path,default=ROOT.parent/'prefix')
    p.add_argument('--wine',type=Path,default=Path.home()/'Applications/Wine Staging.app/Contents/Resources/wine/bin/wine')
    p.add_argument('--output',type=Path,default=ROOT/'analysis/frontend/acceptance-20261026')
    p.add_argument('--timeout',type=float,default=600)
    p.add_argument('--drive',type=int,choices=[0,1,2,3],default=0)
    capture(p.parse_args())

if __name__=='__main__': main()
