// Browser mic talk-back for Gigma rooms.
//
// Up:   mic -> AudioWorklet -> 48 kHz mono 20 ms frames -> Opus (WebCodecs) or,
//       where AudioEncoder/Opus is missing, PCM16 16 kHz -> the same WebSocket
//       used for listening. Frame: codec u8 | 3 reserved | seq u32 LE | payload.
// Down: other guests' mics arrive on that socket as 12-byte-header frames with
//       sampleRate == 0: seq u32 | 0 u32 | codec u8 | 0 | talker u16 | payload.
// The broker forwards mic frames to the producer's plugin (see internal/relay/mic.go).
window.GigmaTalk = (() => {
  const OPUS = 1, PCM16 = 2, FRAME = 960; // 20 ms @ 48k
  let ctx = null, pctx = null, out = null, sendFn = null;
  let stream = null, srcNode = null, capNode = null, sink = null;
  let enc = null, codec = 0, seq = 0, ts = 0, live = false;
  let acc = new Float32Array(FRAME * 8), accN = 0, rsPos = 0, rsPrev = 0;
  let level = 0;
  const players = new Map(); // talker -> { dec, next, lastAt }

  const WORKLET = `class GigmaCap extends AudioWorkletProcessor {
    process(inputs) { const c = inputs[0] && inputs[0][0]; if (c) this.port.postMessage(c.slice(0)); return true; }
  }
  registerProcessor('gigma-cap', GigmaCap);`;

  async function pickEncoder() {
    if (!('AudioEncoder' in window)) return null;
    const base = { codec: 'opus', sampleRate: 48000, numberOfChannels: 1, bitrate: 48000 };
    const tries = [
      { ...base, opus: { frameDuration: 20000, useinbandfec: true, packetlossperc: 10, complexity: 8 } },
      { ...base, opus: { frameDuration: 20000 } },
      base,
    ];
    for (const cfg of tries) {
      try { const r = await AudioEncoder.isConfigSupported(cfg); if (r && r.supported) return cfg; } catch (_) {}
    }
    return null;
  }

  function emit(c, payload) {
    const b = new Uint8Array(8 + payload.byteLength);
    b[0] = c;
    new DataView(b.buffer).setUint32(4, seq >>> 0, true);
    seq = (seq + 1) >>> 0;
    b.set(payload, 8);
    sendFn && sendFn(b);
  }

  function onFrame48(f) {
    let pk = 0;
    for (let i = 0; i < f.length; i++) { const a = Math.abs(f[i]); if (a > pk) pk = a; }
    level = Math.max(pk, level * 0.85);
    if (!live) return;
    if (codec === OPUS && enc && enc.state === 'configured') {
      if (enc.encodeQueueSize > 10) return; // encoder behind: drop rather than lag
      const ad = new AudioData({ format: 'f32-planar', sampleRate: 48000, numberOfFrames: FRAME, numberOfChannels: 1, timestamp: ts, data: f });
      ts += 20000;
      enc.encode(ad); ad.close();
    } else {
      const n = FRAME / 3, pcm = new Int16Array(n); // 48k -> 16k: 3-tap box filter + decimate
      for (let i = 0; i < n; i++) {
        const v = (f[3 * i] + f[3 * i + 1] + f[3 * i + 2]) / 3;
        pcm[i] = Math.max(-32768, Math.min(32767, Math.round(v * 32767)));
      }
      emit(PCM16, new Uint8Array(pcm.buffer));
    }
  }

  // ctx.sampleRate -> 48 kHz (linear), then cut into 20 ms frames.
  function onCapture(chunk) {
    const step = ctx.sampleRate / 48000;
    if (accN + Math.ceil(chunk.length / step) + 2 > acc.length) accN = 0;
    if (step === 1) { acc.set(chunk, accN); accN += chunk.length; }
    else {
      while (rsPos < chunk.length) {
        const i = Math.floor(rsPos), fr = rsPos - i;
        const a = i === 0 ? rsPrev : chunk[i - 1], b = chunk[i];
        acc[accN++] = a + (b - a) * fr;
        rsPos += step;
      }
      rsPos -= chunk.length; rsPrev = chunk[chunk.length - 1];
    }
    while (accN >= FRAME) {
      onFrame48(acc.slice(0, FRAME));
      acc.copyWithin(0, FRAME, accN); accN -= FRAME;
    }
  }

  // Must be called from a user gesture (permission prompt). Returns codec name.
  async function enableMic(audioCtx, send) {
    ctx = audioCtx; sendFn = send;
    if (!stream) {
      try { if (navigator.audioSession) navigator.audioSession.type = 'play-and-record'; } catch (_) {}
      stream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true, channelCount: 1 },
      });
      const cfg = await pickEncoder();
      if (cfg) {
        enc = new AudioEncoder({
          output: (chunk) => { const b = new Uint8Array(chunk.byteLength); chunk.copyTo(b); emit(OPUS, b); },
          error: () => { codec = PCM16; },
        });
        enc.configure(cfg); codec = OPUS;
      } else codec = PCM16;
      srcNode = ctx.createMediaStreamSource(stream);
      sink = ctx.createGain(); sink.gain.value = 0; sink.connect(ctx.destination);
      if (ctx.audioWorklet) {
        const url = URL.createObjectURL(new Blob([WORKLET], { type: 'application/javascript' }));
        await ctx.audioWorklet.addModule(url);
        capNode = new AudioWorkletNode(ctx, 'gigma-cap', { numberOfInputs: 1, numberOfOutputs: 1, channelCount: 1, channelCountMode: 'explicit' });
        capNode.port.onmessage = (e) => onCapture(e.data);
      } else {
        capNode = ctx.createScriptProcessor(1024, 1, 1);
        capNode.onaudioprocess = (e) => onCapture(e.inputBuffer.getChannelData(0).slice(0));
      }
      srcNode.connect(capNode); capNode.connect(sink);
    }
    stream.getAudioTracks().forEach((t) => (t.enabled = true));
    live = true;
    return codec === OPUS ? 'opus' : 'pcm';
  }

  function muteMic() {
    live = false; level = 0;
    if (stream) stream.getAudioTracks().forEach((t) => (t.enabled = false));
  }

  function stopAll() {
    live = false; level = 0;
    try { srcNode && srcNode.disconnect(); capNode && capNode.disconnect(); sink && sink.disconnect(); } catch (_) {}
    if (stream) stream.getTracks().forEach((t) => t.stop());
    try { enc && enc.state !== 'closed' && enc.close(); } catch (_) {}
    for (const p of players.values()) try { p.dec && p.dec.state !== 'closed' && p.dec.close(); } catch (_) {}
    players.clear();
    stream = srcNode = capNode = sink = enc = null; ctx = pctx = out = null; codec = 0; accN = 0; rsPos = 0; rsPrev = 0;
    try { if (navigator.audioSession) navigator.audioSession.type = 'playback'; } catch (_) {}
  }

  // ---- Other guests' mics ----
  function play(p, data, rate) {
    if (!pctx || !out || pctx.state === 'closed') return;
    const ctx = pctx;
    const buf = ctx.createBuffer(1, data.length, rate);
    buf.getChannelData(0).set(data);
    const s = ctx.createBufferSource(); s.buffer = buf; s.connect(out);
    const now = ctx.currentTime;
    if (p.next < now + 0.01 || p.next > now + 0.8) p.next = now + 0.12;
    s.start(p.next); p.next += buf.duration;
  }

  function onMicFrame(ab, playbackCtx, outNode) {
    pctx = playbackCtx; out = outNode;
    if (!playbackCtx || ab.byteLength <= 12) return;
    const dv = new DataView(ab), c = dv.getUint8(8), talker = dv.getUint16(10, true), sq = dv.getUint32(0, true);
    let p = players.get(talker);
    if (!p) { p = { dec: null, next: 0, lastAt: 0 }; players.set(talker, p); }
    p.lastAt = Date.now();
    const payload = new Uint8Array(ab, 12);
    if (c === PCM16) {
      const n = payload.length >> 1, f = new Float32Array(n), v = new DataView(ab, 12);
      for (let i = 0; i < n; i++) f[i] = v.getInt16(2 * i, true) / 32768;
      play(p, f, 16000);
    } else if (c === OPUS && 'AudioDecoder' in window) {
      if (!p.dec) {
        p.dec = new AudioDecoder({
          output: (ad) => {
            const f = new Float32Array(ad.numberOfFrames);
            try { ad.copyTo(f, { planeIndex: 0, format: 'f32-planar' }); play(p, f, ad.sampleRate); } catch (_) {}
            ad.close();
          },
          error: () => { p.dec = null; },
        });
        p.dec.configure({ codec: 'opus', sampleRate: 48000, numberOfChannels: 1 });
      }
      try { p.dec.decode(new EncodedAudioChunk({ type: 'key', timestamp: sq * 20000, data: payload })); } catch (_) {}
    }
  }

  function speaking(talker) { const p = players.get(talker); return !!p && Date.now() - p.lastAt < 600; }
  function micLevel() { return live ? level : 0; }
  function isLive() { return live; }
  function supported() { return !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia); }

  return { enableMic, muteMic, stopAll, onMicFrame, speaking, micLevel, isLive, supported };
})();
