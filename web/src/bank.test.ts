import { describe, expect, it } from 'vitest';
import {
  BANK_HEADER_SIZE,
  BANK_MAGIC,
  BANK_MAX_SAMPLES,
  BANK_SAMPLE_RECORD_SIZE,
  BANK_VERSION,
  BANK_VERSION_WHOLE_BPM,
  buildBankBlob,
  parseBankBlob,
  type BankSample,
} from './bank';
import { estimateBpmFromFrames, inferBeatsFromName, inferBpmFromName } from './audio';

function sample(index: number, pcm = new Uint8Array([index & 0xff])): BankSample {
  return {
    id: String(index),
    name: `Sample ${index + 1}`,
    bpm: 170,
    beats: 8,
    peak: 64,
    pcm,
    cropStart: 0,
    cropEnd: pcm.length,
  };
}

describe('pikocore bank format', () => {
  it('round-trips sample metadata and raw pcm', () => {
    const pcm = new Uint8Array([0, 1, 255, 128]);
    const blob = buildBankBlob(
      [
        {
          id: 'a',
          name: 'Amen',
          bpm: 170,
          beats: 16,
          peak: 127,
          pcm,
          cropStart: 1,
          cropEnd: 3,
        },
      ],
      1024,
    );

    const parsed = parseBankBlob(blob);
    expect(parsed.samples).toHaveLength(1);
    expect(parsed.samples[0].name).toBe('Amen');
    expect(parsed.samples[0].bpm).toBe(170);
    expect(parsed.samples[0].beats).toBe(16);
    expect(Array.from(parsed.samples[0].pcm)).toEqual([1, 255]);
  });

  it('builds and parses a full 128-sample bank', () => {
    const samples = Array.from({ length: BANK_MAX_SAMPLES }, (_, index) => sample(index));
    const blob = buildBankBlob(samples, samples.length);
    const parsed = parseBankBlob(blob);

    expect(blob).toHaveLength(BANK_HEADER_SIZE + samples.length);
    expect(parsed.samples).toHaveLength(BANK_MAX_SAMPLES);
    expect(parsed.samples[127].name).toBe('Sample 128');
    expect(Array.from(parsed.samples[127].pcm)).toEqual([127]);
  });

  it('rejects more than 128 samples', () => {
    const samples = Array.from({ length: BANK_MAX_SAMPLES + 1 }, (_, index) => sample(index));

    expect(() => buildBankBlob(samples, samples.length)).toThrow('pikocore supports up to 128 samples');
  });

  it('rejects v1 4 KiB banks as unsupported', () => {
    const blob = new Uint8Array(4096);
    const view = new DataView(blob.buffer);
    view.setUint32(0, BANK_MAGIC, true);
    view.setUint32(4, 1, true);
    view.setUint32(8, 4096, true);

    expect(() => parseBankBlob(blob)).toThrow('Unsupported pikocore bank');
  });

  it('rejects banks that exceed audio capacity', () => {
    expect(() => buildBankBlob([sample(0, new Uint8Array([1, 2]))], 1)).toThrow('Audio bank exceeds device capacity');
  });

  it('keeps all sample records inside the v2 header', () => {
    expect(32 + BANK_MAX_SAMPLES * BANK_SAMPLE_RECORD_SIZE).toBeLessThanOrEqual(BANK_HEADER_SIZE);
  });
});

describe('centi-BPM storage', () => {
  it('writes version 3 and keeps two decimals', () => {
    const pcm = new Uint8Array([1, 2, 3, 4]);
    const blob = buildBankBlob(
      [{ id: 'a', name: 'Half time', bpm: 87.5, beats: 8, peak: 100, pcm, cropStart: 0, cropEnd: pcm.length }],
      1000,
    );
    const view = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
    expect(view.getUint32(4, true)).toBe(BANK_VERSION);
    expect(view.getUint16(32 + 8, true)).toBe(8750);
    expect(parseBankBlob(blob).samples[0].bpm).toBeCloseTo(87.5, 5);
  });

  it('reads a version 2 bank by scaling whole BPM up', () => {
    const pcm = new Uint8Array([1, 2, 3, 4]);
    const blob = buildBankBlob(
      [{ id: 'a', name: 'Old', bpm: 170, beats: 8, peak: 100, pcm, cropStart: 0, cropEnd: pcm.length }],
      1000,
    );
    const view = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
    // Rewrite it the way firmware before this change stored it.
    view.setUint32(4, BANK_VERSION_WHOLE_BPM, true);
    view.setUint16(32 + 8, 170, true);

    const parsed = parseBankBlob(blob);
    expect(parsed.samples[0].bpm).toBe(170);
  });

  it('rejects a bank version it does not know', () => {
    const pcm = new Uint8Array([1]);
    const blob = buildBankBlob(
      [{ id: 'a', name: 'Future', bpm: 120, beats: 8, peak: 10, pcm, cropStart: 0, cropEnd: pcm.length }],
      1000,
    );
    new DataView(blob.buffer, blob.byteOffset, blob.byteLength).setUint32(4, 99, true);
    expect(() => parseBankBlob(blob)).toThrow('Unsupported pikocore bank');
  });
});

describe('BPM detection', () => {
  it('reads every common filename spelling', () => {
    expect(inferBpmFromName('break_bpm170.wav')).toBe(170);
    expect(inferBpmFromName('break_120bpm.wav')).toBe(120);
    expect(inferBpmFromName('Loop 128 BPM.wav')).toBe(128);
    expect(inferBpmFromName('house-124bpm-loop.aif')).toBe(124);
    expect(inferBpmFromName('halftime_87.5bpm.wav')).toBe(87.5);
    expect(inferBpmFromName('BPM_95_kit.wav')).toBe(95);
  });

  it('returns null when the name carries no bpm', () => {
    expect(inferBpmFromName('amen_break.wav')).toBeNull();
    expect(inferBpmFromName('take 3.wav')).toBeNull();
  });

  it('prefers filename beats', () => {
    expect(inferBeatsFromName('amen_beats16_bpm170.wav')).toBe(16);
  });

  it('estimates bpm from duration, first count that lands in range', () => {
    // 21.94 s: 4, 8 and 16 beats are all too slow, 32 beats gives 87.5 BPM.
    expect(estimateBpmFromFrames(526629)).toBeCloseTo(87.5, 3);
    // 2 s: 4 beats is already 120 BPM.
    expect(estimateBpmFromFrames(48000)).toBeCloseTo(120, 6);
  });

  it('gives up when no division lands in range', () => {
    // 0.2 s: even 4 beats would be 1200 BPM.
    expect(estimateBpmFromFrames(4800)).toBeNull();
    // 60 s: 32 beats is still only 32 BPM.
    expect(estimateBpmFromFrames(24000 * 60)).toBeNull();
  });
});
