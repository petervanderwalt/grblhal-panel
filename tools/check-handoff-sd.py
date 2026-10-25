"""Check real display-SD gCAM handoff without commanding controller motion."""
import argparse, importlib.util, json, time, urllib.request, urllib.error
from pathlib import Path
import serial
parser=argparse.ArgumentParser();parser.add_argument('--port',default='COM12');parser.add_argument('--host',default='192.168.0.34');parser.add_argument('--no-captures',action='store_true');args=parser.parse_args()
spec=importlib.util.spec_from_file_location('remote',Path(__file__).with_name('ui-remote.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
base=f'http://{args.host}:47820/gcam/v1'
def http(path,body=None):
    req=urllib.request.Request(base+path,data=json.dumps(body).encode() if body is not None else None,headers={'Content-Type':'application/json'})
    try:
        with urllib.request.urlopen(req,timeout=5) as res:return res.status,json.load(res)
    except urllib.error.HTTPError as e:return e.code,json.load(e)
with serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5) as p:
    p.dtr=p.rts=False;p.port=args.port;p.open()
    for _ in range(30):
        p.write(b'\nUG SYNC\n');deadline=time.monotonic()+1
        while time.monotonic()<deadline:
            if b'@UGERR command' in p.readline():break
        else:continue
        break
    else:raise TimeoutError('Console sync')
    time.sleep(.8);p.reset_input_buffer()
    def data(s):return json.loads(r.debug(p,s).removeprefix('@UGDATA '))
    def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.4)
    def location(local):
        tap(896,564);tap(830,266);tap(830,313 if local else 350)
        assert data('SD STATE')['local']==local
    try:
        r.debug(p,'DEMO ON');r.debug(p,'TOUCH OFF')
        assert not data('STATE')['job']
        if data('PREFS')['local_preview']:tap(56,112)
        location(True)
        if not args.no_captures:r.exchange(p,'shot',Path('captures/file-location-settings.png'))
        tap(128,564)  # Handoff works while Jog is visible.
        for _ in range(12):
            try:
                status,ping=http('/ping')
                if ping.get('accepting'):break
            except OSError:pass
            time.sleep(1)
        assert ping['destination']=='display-sd' and ping['accepting'],ping
        name=f'gcam-handoff-check-{int(time.time())}.nc'
        payload={'name':name,'gcode':'G21 G90\nG0 X0 Y0\nM30\n'}
        status,result=http('/load',payload)
        assert status==202 and result['destination']=='display-sd',(status,result)
        status,result=http('/status');assert result['success'] and result['bytes']==len(payload['gcode']),result
        assert not data('STATE')['job']
        status,result=http('/load',payload);assert status==409,(status,result)
        status,result=http('/load',{'gcode':'  '});assert status==400
        # Controller selection preserves the old route, unavailable in UART-off demo.
        location(False)
        status,result=http('/ping');assert result['destination']=='controller-sd' and not result['accepting'],result
        status,result=http('/load',payload);assert status==409
        location(True)
        if not args.no_captures:r.exchange(p,'shot',Path('captures/file-location-settings.png'))
        tap(384,564)
        if not args.no_captures:r.exchange(p,'shot',Path('captures/file-location-files.png'))
        print('PASS local upload, status, duplicate protection, empty input, no auto-run and controller routing; SD test file:',name)
    finally:r.debug(p,'TOUCH ON')
