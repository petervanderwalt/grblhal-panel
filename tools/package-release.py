"""Package separate flash regions; never merge across NVS or filesystem storage."""
import argparse,hashlib,json,subprocess,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--version',required=True);p.add_argument('--build',type=Path,default=Path('firmware/viewesmart-p4/build'));p.add_argument('--output',type=Path,default=Path('release'));a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
parts=[]
for src,name,offset,end in [('bootloader/bootloader.bin','bootloader.bin',0x2000,0x8000),('partition_table/partition-table.bin','partition-table.bin',0x8000,0x9000),('lvgl_demo_v9.bin','grblhal-panel.bin',0x10000,0x810000)]:
 data=(a.build/src).read_bytes()
 if not data or offset+len(data)>end:raise ValueError(f'{name} exceeds its flash region')
 (a.output/name).write_bytes(data)
 parts.append(dict(path=name,offset=offset,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
sha=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
manifest=dict(version=a.version,commit=sha,board='viewesmart-7-1024x600-p4-c6',chip='ESP32-P4',idf='5.3.4',flash_size='16MB',parts=parts)
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
(a.output/'SHA256SUMS.txt').write_text(''.join(f"{p['sha256']}  {p['path']}\n" for p in parts),encoding='utf-8')
(a.output/'INSTALL.txt').write_text(f'''grblHAL Panel {a.version}
Hardware: VIEWESMART 7-inch 1024x600 ESP32-P4/C6, tested early P4 revision.
Source: https://github.com/petervanderwalt/grblhal-panel/tree/{sha}
Browser installer: https://petervanderwalt.github.io/grblhal-panel/
Connect the PC to the LCD UART socket. Disconnect the CNC controller first.
Use Chrome or Edge on desktop. Select the CH340 serial port.

Manual first install (esptool 4.8.1):
python -m esptool --chip esp32p4 --port PORT --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x2000 bootloader.bin 0x8000 partition-table.bin 0x10000 grblhal-panel.bin

Manual update of existing Panel installation:
python -m esptool --chip esp32p4 --port PORT --baud 460800 --before default_reset --after hard_reset write_flash 0x10000 grblhal-panel.bin

Do not erase flash or merge these images across NVS. SD files are untouched.
First installation replaces the previous application and partition table.
The C6 needs compatible ESP-Hosted firmware and is not programmed here.
Test controller support with an isolated board before attaching a machine.
''',encoding='utf-8')
with zipfile.ZipFile(a.output/f'grblhal-panel-{a.version}.zip','w',zipfile.ZIP_DEFLATED) as archive:
 for name in [p['path'] for p in parts]+['manifest.json','SHA256SUMS.txt','INSTALL.txt']:
  archive.write(a.output/name,name)
print(json.dumps(manifest,indent=2))
