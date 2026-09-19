import { describe, expect, it } from 'vitest';
import {
  analyzeSampleBpm,
  approximateDeviation,
  autocorrelationTempo,
  bpmCandidates,
  countOnsets,
  octaveDistance,
  spectralFluxEnvelope,
} from './bpm';
import { buildZip, crc32, namedCopyFilename } from './zip';

const SAMPLE_RATE = 24000;

// A loop of short noise bursts: `beats` hits spread over `beats` beats at
// `bpm`, plus quieter sixteenths so the density looks like a breakbeat.
function makeLoop(bpm: number, beats: number, hitsPerBeat = 3): Float32Array {
  const seconds = (beats * 60) / bpm;
  const frames = Math.round(seconds * SAMPLE_RATE);
  const audio = new Float32Array(frames);
  const hitFrames = Math.round(SAMPLE_RATE * 0.01);
  const totalHits = beats * hitsPerBeat;
  let seed = 12345;
  const random = () => {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    return seed / 0x3fffffff - 1;
  };
  for (let hit = 0; hit < totalHits; hit++) {
    const start = Math.round((hit / totalHits) * frames);
    const strong = hit % hitsPerBeat === 0;
    for (let i = 0; i < hitFrames && start + i < frames; i++) {
      const decay = Math.exp(-i / (hitFrames * 0.3));
      audio[start + i] += random() * decay * (strong ? 1 : 0.35);
    }
  }
  return audio;
}

describe('onset envelope and tempo estimate', () => {
  it('finds the pulse of a synthetic loop', () => {
    const audio = makeLoop(160, 8, 1);
    const envelope = spectralFluxEnvelope(audio, SAMPLE_RATE);
    expect(envelope.envelope.length).toBeGreaterThan(10);

    const tempo = autocorrelationTempo(envelope);
    expect(tempo).not.toBeNull();
    // The estimate may land on a related pulse; it must at least be an octave
    // relative of the truth.
    expect(octaveDistance(tempo as number, 160)).toBeLessThan(0.1);
  });

  it('counts roughly one onset per hit', () => {
    const envelope = spectralFluxEnvelope(makeLoop(120, 4, 2), SAMPLE_RATE);
    const onsets = countOnsets(envelope);
    expect(onsets).toBeGreaterThanOrEqual(6);
    expect(onsets).toBeLessThanOrEqual(12);
  });
});

describe('candidate maths', () => {
  it('divides the length into 4, 8, 16 and 32 beats', () => {
    const candidates = bpmCandidates(2); // two seconds
    expect(candidates.map((c) => c.bpm)).toEqual([120, 240, 480, 960]);
  });

  it('treats octave relatives as equal', () => {
    expect(octaveDistance(85, 170)).toBeCloseTo(0, 6);
    expect(octaveDistance(170, 114)).toBeCloseTo(octaveDistance(85, 114), 6);
    expect(approximateDeviation(170, 85)).toBeCloseTo(0, 6);
    expect(approximateDeviation(176.8, 85)).toBeCloseTo(0.04, 2);
  });
});

describe('batch analysis', () => {
  it('takes the BPM from the file name', () => {
    const [result] = analyzeSampleBpm([
      { id: 'a', name: '164.50 bpm Cymbal Break.wav', mono: makeLoop(164.5, 8), sampleRate: SAMPLE_RATE },
    ]);
    expect(result.bpm).toBeCloseTo(164.5, 5);
    expect(result.source).toBe('name');
    expect(result.beats).toBe(8);
    expect(result.flagged).toBe(false);
  });

  it('settles the octave from a same-length sample that names its BPM', () => {
    const named = makeLoop(180, 8);
    const unnamed = makeLoop(180, 8);
    const results = analyzeSampleBpm([
      { id: 'named', name: '180 bpm Angry Loop.wav', mono: named, sampleRate: SAMPLE_RATE },
      { id: 'glitch', name: 'JungleGlitch.wav', mono: unnamed, sampleRate: SAMPLE_RATE },
    ]);
    const glitch = results.find((r) => r.id === 'glitch');
    expect(glitch?.bpm).toBeCloseTo(180, 5);
    // A decision made this way is flagged for review.
    expect(['length', 'analysis']).toContain(glitch?.source);
    if (glitch?.source === 'length') expect(glitch.flagged).toBe(true);
  });

  it('settles the octave from a half-length sample that names its BPM', () => {
    const results = analyzeSampleBpm([
      { id: 'short', name: '170 bpm Short.wav', mono: makeLoop(170, 4), sampleRate: SAMPLE_RATE },
      { id: 'long', name: 'Jungle2.wav', mono: makeLoop(170, 8), sampleRate: SAMPLE_RATE },
    ]);
    const long = results.find((r) => r.id === 'long');
    expect(long?.bpm).toBeCloseTo(170, 5);
    expect(long?.beats).toBe(8);
  });

  it('uses references from samples already in the bank', () => {
    const results = analyzeSampleBpm(
      [{ id: 'glitch', name: 'StretchDnB.wav', mono: makeLoop(180, 8), sampleRate: SAMPLE_RATE }],
      [{ name: '180 Classic Amen', seconds: (8 * 60) / 180, bpm: 180 }],
    );
    expect(results[0].bpm).toBeCloseTo(180, 5);
  });

  it('keeps every result inside the candidate set', () => {
    const results = analyzeSampleBpm([
      { id: 'a', name: 'Unnamed A.wav', mono: makeLoop(172, 8), sampleRate: SAMPLE_RATE },
      { id: 'b', name: 'Unnamed B.wav', mono: makeLoop(96, 4), sampleRate: SAMPLE_RATE },
    ]);
    for (const result of results) {
      const match = result.candidates.some((candidate) => Math.abs(candidate.bpm - result.bpm) < 0.01);
      expect(match).toBe(true);
      expect(result.bpm).toBeGreaterThan(0);
    }
  });
});

describe('zip writer', () => {
  it('stores entries with a working CRC and directory', () => {
    const first = new Uint8Array([1, 2, 3, 4, 5]);
    const second = new TextEncoder().encode('hello');
    const zip = buildZip([
      { name: 'a_120.00bpm.wav', data: first },
      { name: 'b.wav', data: second },
    ]);
    const view = new DataView(zip.buffer);

    expect(view.getUint32(0, true)).toBe(0x04034b50);
    expect(view.getUint32(14, true)).toBe(crc32(first));
    expect(view.getUint32(18, true)).toBe(first.length);

    // End of central directory: two entries, offset points at the directory.
    const eocd = zip.length - 22;
    expect(view.getUint32(eocd, true)).toBe(0x06054b50);
    expect(view.getUint16(eocd + 8, true)).toBe(2);
    const centralStart = view.getUint32(eocd + 16, true);
    expect(view.getUint32(centralStart, true)).toBe(0x02014b50);
    expect(view.getUint32(eocd + 12, true)).toBe(eocd - centralStart);

    // The second entry's local header sits where the directory says it does.
    const secondOffset = view.getUint32(centralStart + 46 + 'a_120.00bpm.wav'.length + 42, true);
    expect(view.getUint32(secondOffset, true)).toBe(0x04034b50);
    expect(view.getUint32(secondOffset + 14, true)).toBe(crc32(second));
  });

  it('matches a known CRC32', () => {
    expect(crc32(new TextEncoder().encode('123456789'))).toBe(0xcbf43926);
  });

  it('appends the BPM only when the name does not already carry one', () => {
    expect(namedCopyFilename('Jungle2.wav', 170, false)).toBe('Jungle2_170.00bpm.wav');
    expect(namedCopyFilename('Half.aif', 87.5, false)).toBe('Half_87.50bpm.aif');
    expect(namedCopyFilename('170 bpm JunglistBeat.wav', 170, true)).toBe('170 bpm JunglistBeat.wav');
  });
});
