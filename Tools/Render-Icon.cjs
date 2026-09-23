// Regenerate with Node.js and the sharp package: node Tools/Render-Icon.cjs
const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');
(async () => {
 const directory=path.resolve(__dirname,'../Assets/Icons');
 const svg=fs.readFileSync(path.join(directory,'InputHalo.svg'));
 const sizes=[16,20,24,32,40,48,64,128,256];
 const images=await Promise.all(sizes.map(size=>sharp(svg,{density:384}).resize(size,size).png().toBuffer()));
 const header=Buffer.alloc(6+16*sizes.length);header.writeUInt16LE(1,2);header.writeUInt16LE(sizes.length,4);
 let offset=header.length;
 images.forEach((png,i)=>{const p=6+16*i;header[p]=sizes[i]===256?0:sizes[i];header[p+1]=header[p];header.writeUInt16LE(1,p+4);header.writeUInt16LE(32,p+6);header.writeUInt32LE(png.length,p+8);header.writeUInt32LE(offset,p+12);offset+=png.length;});
 fs.writeFileSync(path.join(directory,'InputHalo.ico'),Buffer.concat([header,...images]));
 await sharp(svg,{density:384}).resize(512,512).png().toFile(path.join(directory,'InputHalo.png'));
})();
