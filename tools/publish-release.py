"""Publish the tagged CI build with the workflow's short-lived GitHub token."""
import json,os,urllib.request,urllib.error
from pathlib import Path
repo=os.environ['REPOSITORY'];tag=os.environ['GITHUB_REF_NAME'];token=os.environ['GH_TOKEN']
base=f'https://api.github.com/repos/{repo}'
def api(url,data=None,method=None,content='application/json'):
 body=json.dumps(data).encode() if isinstance(data,dict) else data
 req=urllib.request.Request(url,data=body,method=method,headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json','Content-Type':content,'User-Agent':'panel-release'})
 return json.load(urllib.request.urlopen(req))
try:r=api(base+'/releases/tags/'+tag)
except urllib.error.HTTPError as e:
 if e.code!=404:raise
 r=api(base+'/releases',dict(tag_name=tag,name='grblHAL Panel '+tag,prerelease=True,body='Tester firmware for the VIEWESMART 7-inch ESP32-P4/C6 display.\n\n[Install in your browser](https://petervanderwalt.github.io/grblhal-panel/) or download the ZIP and follow INSTALL.txt.\n\nUses the LCD UART socket. Preserves Panel settings and SD files; replaces the LCD application. The C6 coprocessor is not flashed.\n\nSee the README for hardware, features and testing limits.'))
for p in sorted(Path('release').iterdir()):
 if not p.is_file():continue
 for old in r.get('assets',[]):
  if old['name']==p.name:
   req=urllib.request.Request(old['url'],method='DELETE',headers={'Authorization':'Bearer '+token,'User-Agent':'panel-release'});urllib.request.urlopen(req).close()
 api(r['upload_url'].split('{')[0]+'?name='+p.name,p.read_bytes(),content='application/octet-stream')
print(r['html_url'])
