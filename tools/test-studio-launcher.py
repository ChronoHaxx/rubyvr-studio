"""Run the real launcher in PowerShell against a native argv recorder.

Source-free fixtures exercise quoting, connected mode and a stale review index.
This tests the launcher boundary, not rendering; test-connected-studio.py covers GL.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/launcher-test'


def main():
    fixture=OUT/'fixture with spaces'
    for folder in ('tools','build','mod-assets'):
        (fixture/folder).mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/'tools/run-studio.ps1',fixture/'tools/run-studio.ps1')
    starter=fixture/'mod-assets/voxel-world-v6.json'
    starter.write_text('{"version":6,"patterns":[]}\n')
    generator='''import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out');a=p.parse_args()
target=Path(a.out);target.parent.mkdir(parents=True,exist_ok=True)
target.write_text('{"version":7,"patterns":[],"terrain":{"maps":[]}}\\n')
print('Generated synthetic launcher fixture')
'''
    (fixture/'tools/build-terrain-region-example.py').write_text(generator)
    (fixture/'tools/coverage-ledger.py').write_text("import sys\nprint('Synthetic stale coverage index',file=sys.stderr)\nsys.exit(1)\n")
    native=OUT/'argv.c'
    native.write_text('''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char** argv) {
    const char* path=getenv("RUBYVR_LAUNCHER_ARGV");
    FILE* f=path?fopen(path,"wb"):NULL;if(!f)return 9;
    for(int i=1;i<argc;++i)fwrite(argv[i],1,strlen(argv[i])+1,f);
    return fclose(f)?10:0;
}
''')
    mingw=Path(os.environ.get('RUBYVR_MINGW_BIN',r'C:\msys64\mingw64\bin'))
    env=os.environ.copy();env['PATH']=str(mingw)+os.pathsep+env.get('PATH','')
    subprocess.run([str(mingw/'gcc.exe'),str(native),'-o',str(fixture/'build/rubyvr_gui.exe')],env=env,check=True)
    candidates=[Path(os.environ.get('WINDIR',r'C:\Windows'))/'System32/WindowsPowerShell/v1.0/powershell.exe']
    if shutil.which('pwsh'):candidates.append(Path(shutil.which('pwsh')))
    shells=list(dict.fromkeys(p.resolve() for p in candidates if p.is_file()))
    assert shells,'No PowerShell installation found'
    results=[]
    for shell in shells:
        common=[str(shell),'-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass']
        version=subprocess.check_output(common+['-Command','$PSVersionTable.PSVersion.ToString()'],text=True).strip()
        for name,connected,stale in [('plain',False,False),('connected',True,False),('stale-review',True,True)]:
            ledger=fixture/'build/coverage/ledger.sqlite'
            if ledger.exists():ledger.unlink()
            if stale:ledger.parent.mkdir(parents=True,exist_ok=True);ledger.write_bytes(b'synthetic')
            record=OUT/f'{version}-{name}-argv.bin'
            if record.exists():record.unlink()
            env['RUBYVR_LAUNCHER_ARGV']=str(record)
            saved=fixture/f'build/personal {version} {name}.json'
            args=['-TerrainRegions','-Connected'] if connected else ['-Fresh']
            proc=subprocess.run(common+['-File',str(fixture/'tools/run-studio.ps1'),*args,'-Out',str(saved),'-Mingw',str(mingw)],
                cwd=ROOT,env=env,capture_output=True,text=True,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
            (OUT/f'{version}-{name}.log').write_text(proc.stdout+'\n'+proc.stderr)
            assert proc.returncode==0,(version,name,proc.stdout,proc.stderr)
            actual=record.read_bytes().decode().rstrip('\0').split('\0')
            source=fixture/'build/terrain-regions/regions.json' if connected else starter
            expected=['--map','MAP_OLDALE_TOWN','--mode','diorama','--overrides',str(source),
                '--out',str(saved),'--review-index','build/coverage/studio/index.json']
            if connected:expected.append('--connected')
            assert actual==expected,(version,name,actual,expected)
            if stale:assert 'Review export failed' in proc.stdout
            results.append(dict(powershell=version,case=name,status='PASS'))
            print(f'PASS PowerShell {version}: {name}; exact native arguments')
    (OUT/'verification.json').write_text(json.dumps(dict(status='PASS',checks=results),indent=2)+'\n')


if __name__=='__main__':main()
