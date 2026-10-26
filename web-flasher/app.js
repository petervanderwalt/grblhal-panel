import {ESPLoader,Transport} from 'esptool-js';
import CryptoJS from 'crypto-js';
import {flashParts,verifiedBytes} from './firmware.js';
const $=id=>document.getElementById(id);
const log=text=>{ $('log').textContent+=text+'\n';$('log').scrollTop=$('log').scrollHeight; };
const status=text=>$('status').textContent=text;
let manifest,transport,loader,busy=false;
function controls(){
  $('connect').disabled=busy||!manifest||!navigator.serial||!!loader;
  $('install').disabled=busy||!loader||!$('confirm').checked;
  $('disconnect').disabled=busy||!transport;
  $('mode').disabled=busy; $('confirm').disabled=busy;
}
async function disconnect(){
  try {if(transport) await transport.disconnect();} finally {transport=null;loader=null;controls();}
}
$('confirm').onchange=controls;
$('connect').onclick=async()=>{
  busy=true;controls();
  try {
    const port=await navigator.serial.requestPort();
    transport=new Transport(port,true);
    status('Connecting to the display…');
    loader=new ESPLoader({transport,baudrate:460800,terminal:{clean(){ $('log').textContent=''; },writeLine:log,write:t=>log(t)}});
    await loader.main();
    if(loader.chip.CHIP_NAME!=='ESP32-P4') throw Error('Wrong chip. Connect the LCD UART socket, not the CNC controller.');
    status('Display connected. Ready to install.');
  } catch(error){ log(error.message);status(error.name==='NotFoundError'?'No port selected.':error.message);await disconnect(); }
  finally{busy=false;controls();}
};
$('disconnect').onclick=async()=>{await disconnect();status('Disconnected.');};
$('install').onclick=async()=>{
  busy=true;controls();$('progress').value=0;
  try {
    const parts=flashParts(manifest,$('mode').value==='update');
    const fileArray=[];
    for(const part of parts){
      status(`Downloading and checking ${part.path}…`);$('progress').removeAttribute('value');
      const response=await fetch(`firmware/${part.path}`,{cache:'no-store'});
      if(!response.ok) throw Error(`Download failed: ${part.path} (${response.status}).`);
      fileArray.push({address:part.offset,data:await verifiedBytes(part,await response.arrayBuffer())});
    }
    status('Installing. Keep the USB cable connected.');
    await loader.writeFlash({fileArray,eraseAll:false,compress:true,flashMode:'dio',flashFreq:'80m',flashSize:'16MB',
      calculateMD5Hash:bytes=>CryptoJS.MD5(CryptoJS.lib.WordArray.create(bytes)).toString(),
      reportProgress:(i,w,total)=>{const percent=Math.round(100*(i+w/total)/parts.length);$('progress').value=percent;status(`Installing ${parts[i].path} — ${percent}%`);}
    });
    await loader.after('custom_reset',false,'D0|R1|W100|R0|W100');await disconnect();$('progress').value=100;
    status('Installed successfully. The display is restarting.');log('Installation complete.');
  } catch(error){status(`Installation failed: ${error.message}`);log(error.stack||error.message);await disconnect();}
  finally{busy=false;controls();}
};
window.addEventListener('beforeunload',event=>{if(busy){event.preventDefault();event.returnValue='';}});
navigator.serial?.addEventListener('disconnect',()=>{if(!busy){loader=null;transport=null;status('USB disconnected.');controls();}});
try {
  const response=await fetch('firmware/manifest.json',{cache:'no-store'});
  if(!response.ok) throw Error('No firmware release is available yet.');
  manifest=await response.json();flashParts(manifest,false);
  $('version').textContent=manifest.version;
  $('source').href=`https://github.com/petervanderwalt/grblhal-panel/releases/tag/${encodeURIComponent(manifest.version)}`;
  status(navigator.serial?'Ready. Connect your display to begin.':'Open this page in Chrome or Edge on a desktop computer.');
} catch(error){status(error.message);log(error.message);}
controls();
