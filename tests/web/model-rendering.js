/* Compare the retained model against arrays captured from the native binding. */
(async function () {
  'use strict';
  const output = document.getElementById('results');
  const lines = [];
  const log = message => { lines.push(message); output.textContent = lines.join('\n'); };
  const check = (value, message) => { if (!value) throw new Error(message); };
  try {
    const response = await fetch('../../build/tests/model-cases.json');
    check(response.ok, 'Run python3 tests/native/model_regression.py first');
    const cases = await response.json();
    const frame = document.createElement('iframe');
    document.getElementById('preview').appendChild(frame);
    const doc = frame.contentDocument;
    doc.open();
    doc.write('<!doctype html><canvas id="screen" width="320" height="240"></canvas><span id="runtime-status"></span><span id="fps-status"></span>');
    doc.close();
    frame.contentWindow.GAME_ASSETS = [];
    const script = doc.createElement('script');
    script.src = '../../emulator/emulator.js?model-test=' + Date.now();
    await new Promise((resolve,reject) => { script.onload=resolve; script.onerror=reject; doc.body.appendChild(script); });
    const g = frame.contentWindow.GameEmulator;
    await g.ready;
    const context = doc.getElementById('screen').getContext('2d');
    const source = [-0.5,-1,0,1,-1,0,0,1,0];
    const indices = [0,2,1], colors = [0x1234], edges = [0,1,1,2,2,0];
    const baseline = g.assetResidentBytes();
    const model = g.create3DModel(source,indices,colors,edges,0x4321,false);
    check(g.assetResidentBytes() === baseline+92, 'model byte budget');
    source[0]=99; indices[0]=2; colors[0]=1; edges[0]=2;
    const snapshotIndices=[0,2,1], snapshotColors=[0x1234], snapshotEdges=[0,1,1,2,2,0];
    let comparisons=0;
    function start(width,height,viewport) {
      g.present();
      check(g.set3DResolution(width,height), 'resolution');
      check(g.set3DViewport(...viewport), 'viewport');
      g.set3DCamera(0,0,0,0,0,70,0.1,100);
      g.clear(0x2104); g.threeDBeginFrame();
    }
    function pixels() { g.present(); return context.getImageData(0,0,320,240).data; }
    for (const [width,height,viewport] of [[160,120,[0,0,320,240]], [320,240,[0,0,320,240]], [100,80,[13,17,211,173]]]) {
      for (const item of cases) {
        start(width,height,viewport);
        g.draw3DIndexedMesh(item.vertices,snapshotIndices,snapshotColors);
        g.draw3DWireframe(item.vertices,snapshotEdges,0x4321,false);
        g.fillRect(0,0,60,12,0xffff);
        const expected=pixels();
        start(width,height,viewport);
        check(g.draw3DModel(model,item.transform), 'model frame');
        g.fillRect(0,0,60,12,0xffff);
        const actual=pixels();
        check(expected.every((value,index)=>value===actual[index]), `pixels differ: ${width} ${item.transform}`);
        comparisons++;
      }
    }
    log(`PASS ${comparisons} full-frame comparisons against native transformed vertices`);
    check(g.draw3DModel(model,[])===true, 'empty batch');
    check(g.draw3DModel(model,cases[0].transform)===false, 'frame must begin');
    for (let i=0;i<100;i++) {
      start(160,120,[0,0,320,240]);
      g.draw3DModel(model,cases[1].transform.concat(cases[2].transform));
    }
    check(g.assetResidentBytes()===baseline+92, 'steady-state model storage');
    g.releaseAssetHandle(model);
    check(g.assetResidentBytes()===baseline, 'model release');
    log('PASS snapshot, mixed mesh/edges, multiple instances, frame state, memory budget and release');
    log('MODEL RENDERING PASS');
  } catch (error) { log(`FAIL ${error.stack || error}`); }
})();
