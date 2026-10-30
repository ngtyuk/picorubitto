(async function () {
  'use strict';
  const output=document.getElementById('results'), lines=[];
  const log=text=>{lines.push(text);output.textContent=lines.join('\n');};
  function check(ok,message) { if(!ok) throw new Error(message); }
  try {
    const responses=await Promise.all(['../../build/tests/3d-baseline.js','../../emulator/emulator.js'].map(url=>fetch(url,{cache:'no-store'})));
    check(responses.every(r=>r.ok),'Prepare build/tests/3d-baseline.js from commit 692f2c0');
    const sources=await Promise.all(responses.map(r=>r.text()));
    async function fixture(name,source,clip,cache) {
      const frame=document.createElement('iframe');frame.title=name;document.getElementById('previews').appendChild(frame);
      const doc=frame.contentDocument,win=frame.contentWindow;
      doc.open();doc.write('<!doctype html><canvas id="screen" width="320" height="240"></canvas><span id="runtime-status"></span><span id="fps-status"></span>');doc.close();
      win.GAME_ASSETS=[];win.GAME_3D_LINE_CLIP=clip;win.GAME_3D_PROJECTION_CACHE=cache;
      win.testCounts={projections:0,samples:0};
      for(const func of ['threeDProjectVertex','threeDProjectFixedVertex']) {
        source=source.replace(`function ${func}(vertex) {`,`function ${func}(vertex) { window.testCounts.projections++;`);
      }
      const start=source.indexOf('  function threeDRasterLine('),end=source.indexOf('  function threeDTriangleNeedsClipping',start);
      const loop=source.slice(start,end).includes('for (let sample')?'for (let sample = begin; ; sample++) {':'for (;;) {';
      source=source.slice(0,start)+source.slice(start,end).replace(loop,loop+' window.testCounts.samples++;')+source.slice(end);
      source=source.replace('window.GameEmulator = {',`window.GameEmulator = {
        testDepth() { return threeDDepthBuffer.slice(); },
        testLine(a,b,depth) { threeDRasterLine(a,b,0xffff,depth); },`);
      const url=URL.createObjectURL(new Blob([source],{type:'text/javascript'}));
      const script=doc.createElement('script');script.src=url;
      await new Promise((resolve,reject)=>{script.onload=resolve;script.onerror=reject;doc.body.appendChild(script);});
      URL.revokeObjectURL(url);
      const g=win.GameEmulator;await g.ready;
      const context=doc.getElementById('screen').getContext('2d');
      return {name,g,counts:win.testCounts,start(width,height,viewport) {
        g.present();check(g.set3DResolution(width,height),'resolution');check(g.set3DViewport(...viewport),'viewport');
        g.set3DCamera(0,0,0,0,0,70,0.1,100);g.clear(0x2104);g.threeDBeginFrame();
      },image() {g.present();return context.getImageData(0,0,320,240).data;}};
    }
    const fixtures=[];
    for(const [name,which,clip,cache] of [['old',0,false,false],['reference',1,false,false],['clip',1,true,false],['cache',1,false,true],['both',1,true,true]]) fixtures.push(await fixture(name,sources[which],clip,cache));
    const modes=[[160,120,[0,0,320,240]],[320,240,[0,0,320,240]],[13,11,[17,23,211,173]]];
    let comparisons=0,seed=12345;
    const random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed;};
    for(let test=0;test<120;test++) {
      const mode=modes[test%3];
      const coords=Array.from({length:8},()=>random()%2400-1100);
      let expected,depth;
      for(const [index,f] of fixtures.entries()) {
        f.start(...mode);
        if(test<60) {
          for(let i=0;i<coords.length;i+=4) {
            const a={xFixed:coords[i]*65536,yFixed:coords[i+1]*65536,inverseZFixed:25000};
            const b={xFixed:coords[i+2]*65536,yFixed:coords[i+3]*65536,inverseZFixed:65000};
            f.g.testLine(a,b,test%2===0);f.g.testLine(b,a,true);
          }
        } else {
          const count=test%4===0?256:8,points=[],edges=[];
          for(let i=0;i<count;i++) {
            points.push((i%4-2)*0.7,(Math.floor(i/4)%4-2)*0.6,3+(i%3)*0.5);
            edges.push(i,(i+1)%count,i,(i+3)%count);
          }
          if(test%7===0) points[2]=0.05;
          if(test%11===0) points[5]=120;
          if(test%13===0) points[0]=40000;
          if(test%17===0) points[2]=-1;
          f.g.draw3DWireframe(points,edges,0xff20,true);
          points[0]+=0.4;f.g.set3DCamera(0.2,0.1,0,0.1,0.05,70,0.1,100);
          f.g.draw3DWireframe(points,edges,0x07ff,false);
          f.g.draw3DIndexedMesh(points,[0,2,1,1,2,3,0,1,2,0,4,1],[0xf800,0x07e0,0x001f,0xffe0]);
          f.g.draw3DTriangle(-1,-1,2,0,1,2,1,-1,2,0xffff);
        }
        const image=f.image(),z=f.g.testDepth();
        if(index===0) {expected=image;depth=z;}
        else {
          check(expected.every((v,i)=>v===image[i]),`color mismatch ${test} ${f.name}`);
          check(depth.every((v,i)=>v===z[i]),`depth mismatch ${test} ${f.name}`);comparisons++;
        }
      }
    }
    const [old,reference,clip,cache,both]=fixtures;
    check(cache.counts.projections<reference.counts.projections,'projection count did not fall');
    check(clip.counts.samples<reference.counts.samples,'line samples did not fall');
    for(const f of [clip,both]) {
      f.start(320,240,[0,0,320,240]);
      const a={xFixed:-1e12*65536,yFixed:-1e12*65536,inverseZFixed:65536};
      const b={xFixed:1e12*65536,yFixed:1e12*65536,inverseZFixed:65536};
      const before=f.counts.samples;f.g.testLine(a,b,true);
      check(f.counts.samples-before<=320,'huge line was not bounded');
      const pixels=f.image();
      // At least the 240 diagonal positions must have been drawn.
      for(let y=0;y<240;y++) check(pixels[(y*320+y)*4]===255,'huge line phase');
    }
    log(`PASS ${comparisons} full color/depth comparisons`);
    for(const f of fixtures) log(`${f.name}: projections=${f.counts.projections} line_samples=${f.counts.samples}`);
    log('3D REGRESSION PASS');
  } catch(error) {log(`FAIL ${error.stack||error}`);}
})();
