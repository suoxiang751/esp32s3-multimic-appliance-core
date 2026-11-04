"""Compile extracted production modules against deterministic host test ports."""
import argparse,json,os,shutil,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',default='gcc');p.add_argument('--cxx',default='g++')
a=p.parse_args();config=json.loads((root/'tests/targets.json').read_text())
build=root/'.host-build';build.mkdir(exist_ok=True)
for target in config:
    compiler=shutil.which(a.cxx if target.get('cpp') else a.cc)
    if not compiler:raise SystemExit('Compiler not found; pass --cc or --cxx with its path')
    out=build/(target['name']+('.exe' if os.name=='nt' else ''))
    cmd=[compiler,'-std=c++20' if target.get('cpp') else '-std=c11','-Wall','-Wextra','-Werror']
    cmd+=target.get('flags',[])
    cmd+=['-I'+str(root/i) for i in target['includes']]
    cmd+=[str(root/s) for s in target['sources']]+['-o',str(out)]
    env=os.environ.copy();env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
    subprocess.run(cmd,cwd=root,env=env,check=True)
    subprocess.run([str(out)],cwd=root,env=env,check=True)
print('PASS: all host targets; see VERIFICATION.md for hardware boundaries')
