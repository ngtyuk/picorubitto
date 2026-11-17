/* Independent barycentric reference: normalized UV and perspective division. */
(async () => {
  const output=document.getElementById('results');
  const check=(ok,message)=>{if(!ok)throw new Error(message);};
  try {
    const frame=document.createElement('iframe');document.getElementById('preview').appendChild(frame);
    const win=frame.contentWindow,doc=frame.contentDocument;
    doc.open();doc.write('<!doctype html><canvas id="screen" width="320" height="240"></canvas><span id="runtime-status"></span><span id="fps-status"></span>');doc.close();
    win.GAME_ASSET_BASE='../../build/tests/';win.GAME_ASSETS=['texture3d-reference.raw'];
    const script=doc.createElement('script');script.src='../../emulator/emulator.js';
    await new Promise((resolve,reject)=>{script.onload=resolve;script.onerror=reject;doc.body.appendChild(script);});
    const g=win.GameEmulator;await g.ready;
    const texture=g.loadTexture('texture3d-reference.raw',32,32,0xf81f);
    const context=doc.querySelector('canvas').getContext('2d');
    const edge=(a,b,x,y)=>(x-a[0])*(b[1]-a[1])-(y-a[1])*(b[0]-a[0]);
    let samples=0;
    for(let test=0;test<32;test++) {
      const width=test%2?160:320,height=test%2?120:240,perspective=!!(Math.floor(test/2)%2);
      g.present();g.set3DResolution(width,height);g.set3DViewport(0,0,width,height);
      g.set3DCamera(0,0,0,0,0,70,0.2,40);g.clear(0);g.threeDBeginFrame();
      const vertices=[[-1.3+Math.floor(test/4)*0.15,-0.9,2,0.03,0.97],
        [1.8,-0.7,5,0.94,0.82],[0.1-Math.floor(test/4)*0.1,1.1,3.5,0.2,0.04]];
      g.draw3DTexturedTriangles(texture,vertices.flat(),perspective);g.present();
      const image=context.getImageData(0,0,320,240).data;
      const focal=width*0.5/Math.tan(70*Math.PI/360);
      const p=vertices.map(([x,y,z])=>[width*0.5+x*focal/z,height*0.5-y*focal/z]);
      const area=edge(p[0],p[1],...p[2]);
      for(let y=0;y<height;y++)for(let x=0;x<width;x++) {
        const w=p.map((_,i)=>edge(p[(i+1)%3],p[(i+2)%3],x+0.5,y+0.5)/area);
        if(w.some(n=>Math.abs(n)<0.00002))continue;
        let color=0;
        if(w.every(n=>n>=0)) {
          let u=0,v=0,q=0;
          vertices.forEach((vertex,i)=>{const f=perspective?1/vertex[2]:1;u+=w[i]*vertex[3]*f;v+=w[i]*vertex[4]*f;q+=w[i]/vertex[2];});
          if(perspective){u/=q;v/=q;}
          if(Math.abs(u*32-Math.round(u*32))<0.0002||Math.abs(v*32-Math.round(v*32))<0.0002)continue;
          color=1+Math.floor(v*32)*32+Math.floor(u*32);
        }
        const expected=[Math.round(((color>>11)&31)*255/31),Math.round(((color>>5)&63)*255/63),Math.round((color&31)*255/31)];
        const offset=(y*320+x)*4;
        check(expected.every((n,i)=>n===image[offset+i]),`case ${test} pixel ${x},${y}`);samples++;
      }
    }
    // Full-screen clipping and mixed 3D/2D order must preserve the HUD.
    g.set3DResolution(160,120);g.set3DViewport(0,0,320,240);g.clear(0);g.threeDBeginFrame();
    g.draw3DTexturedTriangles(texture,[-1,-1,0.1,0,1,1,-1,2,1,1,0,1,60,0.5,0],true);
    g.fillRect(0,0,20,10,0xffff);g.present();
    const hud=context.getImageData(0,0,1,1).data;check(hud[0]===255&&hud[1]===255&&hud[2]===255,'HUD order');
    g.releaseAssetHandle(texture);
    output.textContent=`PASS ${samples} independent color samples; affine/perspective, both resolutions, clipping and HUD\nTHREE D TEXTURES PASS`;
  } catch(error) {output.textContent=`FAIL ${error.stack||error}`;}
})();
