/* Old renderer versus current Model preparation sharing, in actual Canvas. */
(async function () {
  'use strict';
  const output = document.getElementById('results'), lines = [];
  const log = message => { lines.push(message); output.textContent = lines.join('\n'); };
  const check = (value,message) => { if (!value) throw new Error(message); };
  try {
    const sources = await Promise.all(['../../build/tests/model-shared-baseline.js','../../emulator/emulator.js']
      .map(async path => { const r=await fetch(path); check(r.ok,'Generate model-shared-baseline.js from 6acee9e'); return r.text(); }));
    const fixtures=[];
    for (const [name,which,shared,cache] of [['old',0,false,true],['off',1,false,true],['on',1,true,true],['nocache',1,true,false]]) {
      const frame=document.createElement('iframe');frame.title=name;document.getElementById('previews').appendChild(frame);
      const doc=frame.contentDocument,win=frame.contentWindow;
      doc.open();doc.write('<!doctype html><canvas id="screen" width="320" height="240"></canvas><span id="runtime-status"></span><span id="fps-status"></span>');doc.close();
      win.GAME_ASSETS=[];win.GAME_3D_MODEL_SHARED=shared;win.GAME_3D_PROJECTION_CACHE=cache;
      win.testCounts={camera:0,projections:0};
      let source=sources[which];
      for (const [func,counter] of [['threeDTransformVertex','camera'],['threeDTransformVertexFixed','camera'],['threeDProjectVertex','projections'],['threeDProjectFixedVertex','projections']]) {
        const pattern=new RegExp(`(function ${func}\\([^)]*\\) \\{)`);
        check(pattern.test(source),`missing ${func}`);
        source=source.replace(pattern,`$1 window.testCounts.${counter}++;`);
      }
      source=source.replace('window.GameEmulator = {','window.GameEmulator = { testDepth() { return threeDDepthBuffer.slice(); },');
      const url=URL.createObjectURL(new Blob([source],{type:'text/javascript'}));
      const script=doc.createElement('script');script.src=url;
      await new Promise((resolve,reject)=>{script.onload=resolve;script.onerror=reject;doc.body.appendChild(script);});URL.revokeObjectURL(url);
      const g=win.GameEmulator;await g.ready;
      fixtures.push({name,g,counts:win.testCounts,context:doc.getElementById('screen').getContext('2d')});
    }
    let comparisons=0;
    const modes=[[160,120,[0,0,320,240]],[320,240,[0,0,320,240]],[13,11,[17,23,211,173]]];
    function start(g,test) {
      const [w,h,v]=modes[test%3];g.present();check(g.set3DResolution(w,h),'resolution');check(g.set3DViewport(...v),'viewport');
      g.set3DCamera(0,0,0,0,0,70,0.1,100);g.clear(0x2104);g.threeDBeginFrame();
    }
    for (let test=0;test<90;test++) {
      const count=test%7===0?256:8,vertices=[],indices=[],colors=[],edges=[];
      for (let i=0;i<count;i++) {
        vertices.push((i%4-2)*0.5,(Math.floor(i/4)%4-2)*0.4,3+(i%3)*0.3);
        if(test%4!==1&&test%4!==3) { indices.push(i,(i+1)%count,(i+3)%count);colors.push((i*97+0x1000)&0xffff); }
        if(test%4!==2&&test%4!==3) edges.push(i,(i+1)%count,i,(i+3)%count);
      }
      const pose=[0,0,test%13===0?120:test%17===0?-5:test%7===0?-2.95:1,
        (test%19)*0.13,0.1,-0.2,test%11===0?-1:test%23===0?32768:1,0.7,1.2];
      let expected,depth;
      for (const [index,f] of fixtures.entries()) {
        const {g}=f;start(g,test);const baseline=g.assetResidentBytes();
        const id=g.create3DModel(vertices,indices,colors,edges,0xffff,test%2===0);
        g.draw3DModel(id,pose.concat([0.4,0.2,4,-0.3,0.2,0.1,1,1,1]));
        g.set3DCamera(0.2,0.1,0,0.1,0.05,70,0.1,100);
        pose[0]+=0.25;g.draw3DModel(id,pose);pose[0]-=0.25;
        g.draw3DWireframe([0,0,3,0.2,0.2,3],[0,1],0x07ff,false);
        g.fillRect(0,0,60,12,0xffff);g.present();
        const image=f.context.getImageData(0,0,320,240).data,z=g.testDepth();
        if(index===0) {expected=image;depth=z;}
        else {check(expected.every((v,i)=>v===image[i]),`color ${test} ${f.name}`);check(depth.every((v,i)=>v===z[i]),`depth ${test} ${f.name}`);comparisons++;}
        g.releaseAssetHandle(id);check(g.assetResidentBytes()===baseline,'model release');
      }
    }
    for(const f of fixtures) {
      start(f.g,0);f.counts.camera=f.counts.projections=0;
      const id=f.g.create3DModel([0,0,0,1,0,0,0,1,0],[0,1,2],[0x07e0],[0,1,1,2,2,0],0xffff,true);
      f.g.draw3DModel(id,[0,0,4,0,0,0,1,1,1]);f.g.present();f.g.releaseAssetHandle(id);
      log(`${f.name}: camera=${f.counts.camera} projections=${f.counts.projections}`);
    }
    check(fixtures[0].counts.camera===6&&fixtures[1].counts.camera===6&&fixtures[2].counts.camera===3,'camera count');
    check(fixtures[0].counts.projections===6&&fixtures[1].counts.projections===6&&fixtures[2].counts.projections===3,'projection count');
    log(`PASS ${comparisons} full color/depth comparisons; instances, clipping, fallback, array calls, memory`);
    log('MODEL SHARING PASS');
  } catch(error) {log(`FAIL ${error.stack||error}`);}
})();
