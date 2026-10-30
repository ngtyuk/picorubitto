/* Compare production Canvas/depth against 41a47ed, including numeric fallback. */
(async function () {
  'use strict';
  const output = document.getElementById('results'), lines = [];
  const log = text => { lines.push(text); output.textContent = lines.join('\n'); };
  const check = (value, message) => { if (!value) throw new Error(message); };
  try {
    const sources = await Promise.all(['../../build/tests/triangle-baseline.js','../../emulator/emulator.js'].map(async path => {
      const response = await fetch(path); check(response.ok, 'Generate triangle-baseline.js from 41a47ed'); return response.text();
    }));
    const fixtures = [];
    for (const [name, sourceIndex, mode] of [['old',0,0],['reference',1,0],['rows',1,1],['spans',1,2],['blocks',1,3]]) {
      const frame = document.createElement('iframe'); frame.title = name; document.getElementById('previews').appendChild(frame);
      const win = frame.contentWindow, doc = frame.contentDocument;
      doc.open(); doc.write('<!doctype html><canvas id="screen" width="320" height="240"></canvas><span id="runtime-status"></span><span id="fps-status"></span>'); doc.close();
      win.GAME_ASSETS = []; win.GAME_3D_TRIANGLE_MODE = mode;
      const source = sources[sourceIndex].replace('window.GameEmulator = {', `window.GameEmulator = {
        testDepth() { return threeDDepthBuffer.slice(); },
        testTriangle(points, color) { return threeDRasterTriangle(...points.map(([x,y,z]) => ({x,y,inverseZ:z,xFixed:Math.round(x*65536),yFixed:Math.round(y*65536),inverseZFixed:Math.round(z*65536)})), color); },`);
      const url = URL.createObjectURL(new Blob([source], {type:'text/javascript'}));
      const script = doc.createElement('script'); script.src = url;
      await new Promise((resolve,reject) => { script.onload=resolve; script.onerror=reject; doc.body.appendChild(script); }); URL.revokeObjectURL(url);
      const game = win.GameEmulator; await game.ready;
      fixtures.push({name, game, context:doc.querySelector('canvas').getContext('2d')});
    }
    let seed = 987;
    const random = () => { seed = (Math.imul(seed,1664525)+1013904223) >>> 0; return seed; };
    let comparisons = 0;
    for (let test = 0; test < 240; test += 1) {
      const shapes = [];
      for (let i=0;i<5;i+=1) shapes.push(Array.from({length:3}, () => [(random()%16000-8000)/16,(random()%12000-6000)/16,0.25+i*0.25]));
      if (test%7===0) shapes.push([[1.5,1.5,1],[318.5,237,1],[318.5,236.5,1]]);
      if (test%11===0) shapes.push([[0,0,1],[0,240,1],[320,240,1]],[[0,0,1],[320,240,1],[320,0,1]]);
      if (test%13===0) shapes.push([[-4096,-4096,1],[0,4096,1],[4096,-4096,1]]);
      if (test%17===0) shapes.push([[6.25,7.125,1],[6.5,7.5,1],[6.5,7.125,1]]);
      if (test%19===0) shapes.push([[4,4,1],[14,14,1],[24,24,1]]);
      if (test%23===0) shapes.push([[2,2,1],[318,235,0.75],[317,235.5,0.25]]);
      if (test%29===0) shapes.push([[-4096,0,1e14],[0,4096,1],[4096,0,2]]);
      let expected, expectedDepth;
      for (const [index,fixture] of fixtures.entries()) {
        const g = fixture.game, resolution = test%3;
        g.present(); g.set3DResolution(resolution===0?160:resolution===1?320:13,resolution===0?120:resolution===1?240:11);
        g.set3DViewport(resolution===2?17:0,resolution===2?23:0,resolution===2?211:320,resolution===2?173:240);
        g.set3DCamera(0,0,0,0,0,70,0.1,100); g.clear(0x2104); g.threeDBeginFrame();
        for (const [shapeIndex,points] of shapes.entries()) {
          g.testTriangle(points, 0x1000+shapeIndex*97);
          g.testTriangle(points, 0xffff); // equal depth must keep the first color
          g.testTriangle([points[1],points[0],points[2]],0x07e0);
        }
        g.draw3DTriangle(-1,-1,0.05,0,1,4,1,-1,3,0xf800);
        g.draw3DTriangle(-1,-1,3,0,1,120,1,-1,3,0x07e0);
        g.draw3DTriangle(-40000,-1,4,0,1,4,1,-1,4,0x001f);
        g.fillRect(0,0,16,9,0xffff); g.present();
        const pixels = fixture.context.getImageData(0,0,320,240).data, depth = g.testDepth();
        if (index===0) {expected=pixels;expectedDepth=depth;}
        else {
          check(expected.every((v,i)=>v===pixels[i]),`color test=${test} mode=${fixture.name}`);
          check(expectedDepth.every((v,i)=>v===depth[i]),`depth test=${test} mode=${fixture.name}`); comparisons+=1;
        }
      }
    }
    log(`PASS ${comparisons} full color/depth comparisons; coverage, clipping, tiny/shared edges, numerical fallback, Canvas ordering`);
    log('TRIANGLE SCANS PASS');
  } catch (error) { log(`FAIL ${error.stack||error}`); }
})();
