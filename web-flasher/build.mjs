import {mkdir,copyFile} from 'node:fs/promises';
await mkdir('dist',{recursive:true});
await copyFile('index.html','dist/index.html');
await copyFile('../docs/screenshots/panel-jog.png','dist/jog.png');
