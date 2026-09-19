import { BANK_SAMPLE_RATE, BankSample, signedByteToFloat } from './bank';

// Beat counts tried when the file name says nothing, in order.
const LOOP_BEAT_COUNTS = [4, 8, 16, 32];
const BPM_MIN = 80;
const BPM_MAX = 180;
const TARGET_PEAK = Math.pow(10, -1 / 20);

// Accepts "120bpm", "bpm120", "_120_bpm", "120 BPM" and fractional values.
export function inferBpmFromName(name: string): number | null {
  const patterns = [
    /(\d+(?:\.\d+)?)\s*[_-]?\s*bpm/i,
    /bpm\s*[_-]?\s*(\d+(?:\.\d+)?)/i,
  ];
  for (const pattern of patterns) {
    const match = pattern.exec(name);
    if (!match) continue;
    const bpm = Number(match[1]);
    if (Number.isFinite(bpm) && bpm > 0) return bpm;
  }
  return null;
}

export function inferBeatsFromName(name: string): number | null {
  const match = /beats[_-]?(\d+)/i.exec(name);
  if (!match) return null;
  const beats = Math.round(Number(match[1]));
  return beats > 0 ? beats : null;
}

// Divides the length into 4, 8, 16 and 32 beats and takes the first tempo
// that lands in a usable range. Returns null when none of them do.
export function estimateLoopFromFrames(
  frameCount: number,
): { bpm: number; beats: number } | null {
  const seconds = frameCount / BANK_SAMPLE_RATE;
  if (!(seconds > 0)) return null;
  for (const beats of LOOP_BEAT_COUNTS) {
    const bpm = (beats * 60) / seconds;
    if (bpm >= BPM_MIN && bpm <= BPM_MAX) return { bpm, beats };
  }
  return null;
}

export function estimateBpmFromFrames(frameCount: number): number | null {
  return estimateLoopFromFrames(frameCount)?.bpm ?? null;
}

export async function decodeAndEncodeFile(file: File): Promise<BankSample> {
  const context = new AudioContext();
  const buffer = await context.decodeAudioData(await file.arrayBuffer());
  await context.close();

  const mono = new Float32Array(buffer.length);
  for (let channel = 0; channel < buffer.numberOfChannels; channel++) {
    const data = buffer.getChannelData(channel);
    for (let i = 0; i < data.length; i++) mono[i] += data[i] / buffer.numberOfChannels;
  }

  const monoBuffer = new AudioBuffer({
    length: mono.length,
    numberOfChannels: 1,
    sampleRate: buffer.sampleRate,
  });
  monoBuffer.copyToChannel(mono, 0);

  const outLength = Math.max(1, Math.floor((mono.length / buffer.sampleRate) * BANK_SAMPLE_RATE));
  const offline = new OfflineAudioContext(1, outLength, BANK_SAMPLE_RATE);
  const source = offline.createBufferSource();
  const lowpass = offline.createBiquadFilter();
  lowpass.type = 'lowpass';
  lowpass.frequency.value = 20000;
  lowpass.Q.value = 0.707;
  source.buffer = monoBuffer;
  source.connect(lowpass).connect(offline.destination);
  source.start();
  const rendered = await offline.startRendering();
  const data = rendered.getChannelData(0);

  let peakFloat = 0;
  for (const value of data) peakFloat = Math.max(peakFloat, Math.abs(value));
  const gain = peakFloat > 0 ? TARGET_PEAK / peakFloat : 1;

  const pcm = new Uint8Array(data.length);
  let peak = 0;
  for (let i = 0; i < data.length; i++) {
    const quantized = Math.max(-128, Math.min(127, Math.round(data[i] * gain * 127)));
    peak = Math.max(peak, Math.abs(quantized));
    pcm[i] = Math.max(0, Math.min(255, quantized + 128));
  }

  const estimated = estimateLoopFromFrames(pcm.length);
  // 0 means "no BPM": the row shows it as missing and upload stays blocked.
  const bpm = inferBpmFromName(file.name) ?? estimated?.bpm ?? 0;
  const beats = inferBeatsFromName(file.name) ?? estimated?.beats ?? 8;
  return {
    id: crypto.randomUUID(),
    name: file.name.replace(/\.[^.]+$/, ''),
    bpm,
    beats,
    peak,
    pcm,
    cropStart: 0,
    cropEnd: pcm.length,
  };
}

export function makePreviewBuffer(context: AudioContext, pcm: Uint8Array): AudioBuffer {
  const buffer = context.createBuffer(1, pcm.length, BANK_SAMPLE_RATE);
  const channel = buffer.getChannelData(0);
  for (let i = 0; i < pcm.length; i++) {
    channel[i] = signedByteToFloat(pcm[i]) / 128;
  }
  return buffer;
}
