(async function () {
  'use strict';
  const results = document.getElementById('results');
  const frame = document.createElement('iframe');
  document.body.appendChild(frame);
  const win = frame.contentWindow;
  const doc = frame.contentDocument;
  const gains = [], sources = [];
  function check(value, message) { if (!value) throw new Error(message); }
  try {
    doc.open();
    doc.write('<canvas id="screen" width="320" height="240"></canvas><span id="runtime-status"></span>' +
      '<label>音量ダイヤル<input id="audio-dial" type="range" min="0" max="4095" value="4095"></label>');
    doc.close();
    // A graph spy verifies existing voices receive gain changes and no voice
    // bypasses the master node; it does not claim physical listening coverage.
    win.AudioContext = class {
      constructor() { this.destination = {}; this.currentTime = 0; this.state = 'running'; }
      createGain() {
        const node = { gain: { value: 1, setTargetAtTime(value) { this.target = value; } },
          connect(destination) { this.destination = destination; } };
        gains.push(node); return node;
      }
      async decodeAudioData() { return { duration: 1 }; }
      createBufferSource() {
        const source = { connect(destination) { this.destination = destination; },
          start() {}, stop() { this.onended(); } };
        sources.push(source); return source;
      }
    };
    const wav = new Uint8Array(1024 * 1024);
    const view = new DataView(wav.buffer);
    for (const [offset, value] of [[0, 'RIFF'], [8, 'WAVE'], [12, 'fmt '], [36, 'data']]) {
      for (let i = 0; i < value.length; i++) wav[offset + i] = value.charCodeAt(i);
    }
    view.setUint32(4, wav.length - 8, true); view.setUint32(16, 16, true);
    view.setUint16(20, 1, true); view.setUint16(22, 1, true);
    view.setUint32(24, 22050, true); view.setUint32(28, 22050, true);
    view.setUint16(32, 1, true); view.setUint16(34, 8, true);
    view.setUint32(40, wav.length - 44, true);
    const realFetch = win.fetch.bind(win);
    win.fetch = url => String(url).endsWith('/large.wav')
      ? Promise.resolve({ ok: true, arrayBuffer: async () => wav.buffer }) : realFetch(url);
    win.GAME_ASSETS = ['beep.wav', 'large.wav'];
    win.GAME_ASSET_URLS = { 'beep.wav': new URL('../../examples/games/2d/assets/beep.wav', location.href).href };
    await new Promise((resolve, reject) => {
      const script = doc.createElement('script');
      script.src = new URL('../../emulator/emulator.js', location.href).href;
      script.onload = resolve; script.onerror = reject; doc.body.appendChild(script);
    });
    const game = win.GameEmulator;
    await game.ready;
    check(game.audioDialSupported && game.audioDialValue === 4095 && game.audioMasterVolume === 255, 'defaults');
    for (let raw = 0; raw <= 4095; raw++) {
      game.setAudioDial(raw);
      const expected = raw <= 128 ? 0 : raw >= 3800 ? 255 : Math.floor((raw - 128) * 255 / 3672);
      check(game.audioDialValue === raw && game.audioMasterVolume === expected, 'mapping ' + raw);
    }
    for (const raw of [3800, 3870, 3888, 3890, 3945, 4095]) {
      game.setAudioDial(raw);
      check(game.audioDialValue === raw && game.audioMasterVolume === 255, 'physical upper stop ' + raw);
    }
    for (const raw of [0, 1, 2, 4, 128]) {
      game.setAudioDial(raw);
      check(game.audioDialValue === raw && game.audioMasterVolume === 0, 'physical lower stop ' + raw);
    }
    for (const value of [-1, 4096, 1.5, NaN]) {
      let rejected = false;
      try { game.setAudioDial(value); } catch (_) { rejected = true; }
      check(rejected, 'invalid dial value');
    }
    const dial = doc.getElementById('audio-dial');
    dial.value = 0; dial.dispatchEvent(new win.Event('input', { bubbles: true }));
    check(game.audioMasterVolume === 0, 'slider input');
    const large = game.loadSound('large.wav');
    let rejectedLargeSe = false;
    try { game.soundPlay(large, 255); } catch (_) { rejectedLargeSe = true; }
    check(rejectedLargeSe, 'large WAV rejected for SE');
    game.soundPlayBgm(large, 255, true);
    check(sources.at(-1).loop, '1 MiB BGM loops');
    game.audioStopBgm();
    gains.length = 1; sources.length = 0;
    const sound = game.loadSound('beep.wav');
    for (let voice = 0; voice < 4; voice++) game.soundPlay(sound, 192);
    check(sources.length === 4 && gains.length === 5, 'four voices, one master');
    check(gains[0].gain.target === 0, 'mute before starting voices');
    for (const source of sources) {
      check(source.destination.gain.value === 192 / 255 / 8, 'per-voice gain');
      check(source.destination.destination === gains[0], 'voice routes through master');
    }
    game.soundPlayBgm(sound, 255, true);
    check(sources.length === 5 && sources[4].destination.gain.value === 0.5 && sources[4].loop, 'looping BGM plus four SE');
    game.soundPlayBgm(sound, 128);
    check(sources.length === 6 && sources[5].destination.gain.value === 128 / 255 / 2 && !sources[5].loop, 'BGM replacement');
    game.audioStopBgm();
    check(game.audioBusy, 'stopping BGM preserves SE');
    game.setAudioDial(2048);
    check(gains[0].gain.target === 133 / 255, 'existing voices change gain');
    dial.dispatchEvent(new win.KeyboardEvent('keydown', { key: 'ArrowLeft', bubbles: true, cancelable: true }));
    game.beginInputFrame(); check(game.inputState === 0, 'slider keys do not move game');
    game.audioStop();
    check(!game.audioBusy && game.audioDialValue === 2048 && game.audioMasterVolume === 133, 'stop preserves dial');
    results.textContent = 'PASS: all 4096 ADC codes, invalid values, slider, keyboard, four voices, live master gain, stop';
  } catch (error) {
    results.textContent = 'FAIL: ' + error.stack;
    console.error(error);
  }
})();
