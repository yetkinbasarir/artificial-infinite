// Tempo analysis for freshly added samples.
//
// A file name that states its BPM is taken as given. Otherwise the length is
// divided into 4, 8, 16 and 32 beats and the candidate closest to a spectral
// flux / autocorrelation estimate wins. That estimate usually lands on the
// half-time pulse of a breakbeat, so where a candidate and its double are
// equally close the octave is settled from the batch: a sample whose length
// matches (or halves or doubles) one that names its BPM plays at that BPM,
// then the onsets-per-beat rule, then the 80-180 range.

import { inferBpmFromName } from './audio';

export type BpmSource = 'name' | 'analysis' | 'length' | 'onsets' | 'range';

export const BEAT_COUNTS = [4, 8, 16, 32] as const;
export const PREFERRED_MIN_BPM = 80;
export const PREFERRED_MAX_BPM = 180;
// A breakbeat in this range carries roughly one hit per sixteenth note.
export const ONSETS_PER_BEAT_MIN = 2.5;
export const ONSETS_PER_BEAT_MAX = 3.5;
// Two lengths this close are the same loop, or the same loop twice over.
export const LENGTH_TOLERANCE = 0.01;
// Past this the chosen tempo no longer really agrees with the estimate. The
// estimate often locks onto a related pulse, so the comparison also allows the
// octave and the two-against-three relatives.
export const APPROXIMATE_TOLERANCE = 0.04;
export const AGREEMENT_FACTORS = [1, 2, 0.5, 1.5, 2 / 3] as const;
// Candidates this close in octave terms cannot be told apart by the estimate.
const OCTAVE_TIE = 0.05;

const FRAME_SIZE = 1024;
const HOP_SIZE = 256;
const MIN_TEMPO = 60;
const MAX_TEMPO = 240;
// Autocorrelation peaks just as happily on a bar as on a beat, so lags are
// weighted by a log-normal preference around a middling tempo.
const TEMPO_PRIOR_CENTRE = 120;
const TEMPO_PRIOR_WIDTH = 1.0;

export interface BpmCandidate {
  beats: number;
  bpm: number;
}

export interface BpmAnalysisInput {
  id: string;
  name: string;
  mono: Float32Array;
  sampleRate: number;
}

export interface BpmAnalysis {
  id: string;
  bpm: number;
  beats: number;
  source: BpmSource;
  approximateBpm: number | null;
  onsetsPerBeat: number | null;
  seconds: number;
  candidates: BpmCandidate[];
  flagged: boolean;
}

export function octaveDistance(a: number, b: number): number {
  if (!(a > 0) || !(b > 0)) return Number.POSITIVE_INFINITY;
  const octaves = Math.log2(a / b);
  return Math.abs(octaves - Math.round(octaves));
}

// Smallest distance between the chosen tempo and the estimate once the usual
// half, double and three-against-two relations are allowed.
export function approximateDeviation(bpm: number, approximate: number | null): number {
  if (!approximate || !(approximate > 0) || !(bpm > 0)) return 0;
  let best = Number.POSITIVE_INFINITY;
  for (const factor of AGREEMENT_FACTORS) {
    const deviation = Math.abs(bpm / factor - approximate) / approximate;
    if (deviation < best) best = deviation;
  }
  return best;
}

export function tempoAgrees(bpm: number, approximate: number | null): boolean {
  if (!approximate || !(approximate > 0)) return true;
  return approximateDeviation(bpm, approximate) <= APPROXIMATE_TOLERANCE;
}

export function bpmCandidates(seconds: number): BpmCandidate[] {
  if (!(seconds > 0)) return [];
  return BEAT_COUNTS.map((beats) => ({ beats, bpm: (beats * 60) / seconds }));
}

// In-place iterative radix-2 FFT; `re`/`im` must be a power-of-two length.
function fft(re: Float32Array, im: Float32Array): void {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const tr = re[i];
      re[i] = re[j];
      re[j] = tr;
      const ti = im[i];
      im[i] = im[j];
      im[j] = ti;
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const angle = (-2 * Math.PI) / len;
    const wRe = Math.cos(angle);
    const wIm = Math.sin(angle);
    for (let i = 0; i < n; i += len) {
      let curRe = 1;
      let curIm = 0;
      for (let k = 0; k < len / 2; k++) {
        const aRe = re[i + k];
        const aIm = im[i + k];
        const bRe = re[i + k + len / 2] * curRe - im[i + k + len / 2] * curIm;
        const bIm = re[i + k + len / 2] * curIm + im[i + k + len / 2] * curRe;
        re[i + k] = aRe + bRe;
        im[i + k] = aIm + bIm;
        re[i + k + len / 2] = aRe - bRe;
        im[i + k + len / 2] = aIm - bIm;
        const nextRe = curRe * wRe - curIm * wIm;
        curIm = curRe * wIm + curIm * wRe;
        curRe = nextRe;
      }
    }
  }
}

export interface OnsetEnvelope {
  envelope: Float32Array;
  hopSeconds: number;
}

// Half-wave rectified spectral flux.
export function spectralFluxEnvelope(mono: Float32Array, sampleRate: number): OnsetEnvelope {
  const hopSeconds = HOP_SIZE / sampleRate;
  if (mono.length < FRAME_SIZE) return { envelope: new Float32Array(0), hopSeconds };

  const window = new Float32Array(FRAME_SIZE);
  for (let i = 0; i < FRAME_SIZE; i++) {
    window[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (FRAME_SIZE - 1));
  }

  const frames = Math.floor((mono.length - FRAME_SIZE) / HOP_SIZE) + 1;
  const envelope = new Float32Array(frames);
  const bins = FRAME_SIZE / 2;
  let previous = new Float32Array(bins);
  const re = new Float32Array(FRAME_SIZE);
  const im = new Float32Array(FRAME_SIZE);

  for (let frame = 0; frame < frames; frame++) {
    const offset = frame * HOP_SIZE;
    for (let i = 0; i < FRAME_SIZE; i++) {
      re[i] = mono[offset + i] * window[i];
      im[i] = 0;
    }
    fft(re, im);

    let flux = 0;
    const magnitude = new Float32Array(bins);
    for (let k = 0; k < bins; k++) {
      const value = Math.sqrt(re[k] * re[k] + im[k] * im[k]);
      magnitude[k] = value;
      const rise = value - previous[k];
      if (rise > 0) flux += rise;
    }
    envelope[frame] = flux;
    previous = magnitude;
  }
  return { envelope, hopSeconds };
}

// Strongest autocorrelation lag of the onset envelope, as BPM.
export function autocorrelationTempo(
  { envelope, hopSeconds }: OnsetEnvelope,
  minBpm = MIN_TEMPO,
  maxBpm = MAX_TEMPO,
): number | null {
  if (envelope.length < 8 || !(hopSeconds > 0)) return null;
  let mean = 0;
  for (const value of envelope) mean += value;
  mean /= envelope.length;
  const centred = Float32Array.from(envelope, (value) => value - mean);

  const minLag = Math.max(1, Math.floor(60 / (maxBpm * hopSeconds)));
  const maxLag = Math.min(centred.length - 1, Math.ceil(60 / (minBpm * hopSeconds)));
  if (maxLag <= minLag) return null;

  let bestLag = -1;
  let bestValue = -Infinity;
  const values = new Float32Array(maxLag + 1);
  for (let lag = minLag; lag <= maxLag; lag++) {
    let sum = 0;
    for (let i = 0; i + lag < centred.length; i++) sum += centred[i] * centred[i + lag];
    // Longer lags overlap less, so normalise by the overlap.
    const bpm = 60 / (lag * hopSeconds);
    const octaves = Math.log2(bpm / TEMPO_PRIOR_CENTRE) / TEMPO_PRIOR_WIDTH;
    const prior = Math.exp(-0.5 * octaves * octaves);
    const normalised = (sum / (centred.length - lag)) * prior;
    values[lag] = normalised;
    if (normalised > bestValue) {
      bestValue = normalised;
      bestLag = lag;
    }
  }
  if (bestLag < 0 || bestValue <= 0) return null;

  // Parabolic interpolation for a lag between frames.
  let lag = bestLag;
  if (bestLag > minLag && bestLag < maxLag) {
    const left = values[bestLag - 1];
    const right = values[bestLag + 1];
    const denominator = left - 2 * bestValue + right;
    if (denominator !== 0) lag += (0.5 * (left - right)) / denominator;
  }
  return 60 / (lag * hopSeconds);
}

// Peaks of the onset envelope, counted with a minimum spacing.
export function countOnsets({ envelope, hopSeconds }: OnsetEnvelope): number {
  if (envelope.length === 0) return 0;
  let mean = 0;
  for (const value of envelope) mean += value;
  mean /= envelope.length;
  let variance = 0;
  for (const value of envelope) variance += (value - mean) * (value - mean);
  const deviation = Math.sqrt(variance / envelope.length);
  const threshold = mean + 0.6 * deviation;
  const minSpacing = Math.max(1, Math.round(0.03 / hopSeconds));

  let count = 0;
  let lastPeak = -minSpacing;
  for (let i = 1; i < envelope.length - 1; i++) {
    const value = envelope[i];
    if (value < threshold) continue;
    if (value < envelope[i - 1] || value < envelope[i + 1]) continue;
    if (i - lastPeak < minSpacing) continue;
    count++;
    lastPeak = i;
  }
  return count;
}

// Distance to a reference tempo in octaves: the reference itself wins over its
// half or double, but those still beat anything further away.
function referenceDistance(bpm: number, reference: number): number {
  if (!(bpm > 0) || !(reference > 0)) return Number.POSITIVE_INFINITY;
  return Math.abs(Math.log2(bpm / reference));
}

function lengthsMatch(a: number, b: number): boolean {
  for (const multiple of [1, 2, 0.5]) {
    const expected = b * multiple;
    if (Math.abs(a - expected) <= expected * LENGTH_TOLERANCE) return true;
  }
  return false;
}

interface Measured {
  input: BpmAnalysisInput;
  seconds: number;
  candidates: BpmCandidate[];
  approximateBpm: number | null;
  onsets: number;
  namedBpm: number | null;
}

/**
 * Analyses a batch together: named samples in the batch (and any references
 * passed in) settle the octave for the rest.
 */
export function analyzeSampleBpm(
  inputs: BpmAnalysisInput[],
  references: { name: string; seconds: number; bpm: number }[] = [],
): BpmAnalysis[] {
  const measured: Measured[] = inputs.map((input) => {
    const seconds = input.sampleRate > 0 ? input.mono.length / input.sampleRate : 0;
    const namedBpm = inferBpmFromName(input.name);
    const needsAnalysis = namedBpm == null;
    const onsetEnvelope = needsAnalysis
      ? spectralFluxEnvelope(input.mono, input.sampleRate)
      : null;
    return {
      input,
      seconds,
      candidates: bpmCandidates(seconds),
      approximateBpm: onsetEnvelope ? autocorrelationTempo(onsetEnvelope) : null,
      onsets: onsetEnvelope ? countOnsets(onsetEnvelope) : 0,
      namedBpm,
    };
  });

  // Anything with a stated BPM can settle another sample's octave.
  const named = [
    ...references,
    ...measured
      .filter((item) => item.namedBpm != null)
      .map((item) => ({ name: item.input.name, seconds: item.seconds, bpm: item.namedBpm as number })),
  ];

  return measured.map((item) => {
    const { input, seconds, candidates, approximateBpm, onsets, namedBpm } = item;
    const base = { id: input.id, seconds, candidates, approximateBpm };

    if (namedBpm != null) {
      return {
        ...base,
        bpm: namedBpm,
        beats: Math.max(1, Math.round((namedBpm * seconds) / 60)),
        source: 'name' as const,
        onsetsPerBeat: null,
        flagged: false,
      };
    }

    if (candidates.length === 0) {
      return { ...base, bpm: 0, beats: 8, source: 'analysis' as const, onsetsPerBeat: null, flagged: true };
    }

    const inRange = candidates.filter(
      (candidate) => candidate.bpm >= PREFERRED_MIN_BPM && candidate.bpm <= PREFERRED_MAX_BPM,
    );
    const pool = inRange.length > 0 ? inRange : candidates;
    const scored = pool
      .map((candidate) => ({ candidate, distance: octaveDistance(candidate.bpm, approximateBpm ?? candidate.bpm) }))
      .sort((a, b) => a.distance - b.distance || b.candidate.beats - a.candidate.beats);

    let chosen = scored[0].candidate;
    let source: BpmSource = 'analysis';
    const tied = scored.filter((entry) => entry.distance - scored[0].distance < OCTAVE_TIE);

    if (tied.length > 1) {
      // 1. A sample of the same (or half, or double) length that names its BPM.
      const match = named.find((reference) => lengthsMatch(seconds, reference.seconds));
      // The reference only settles which candidate to take; the tempo itself
      // always comes from this sample's own length.
      const byLength = match
        ? tied.reduce((best, entry) =>
            referenceDistance(entry.candidate.bpm, match.bpm) <
            referenceDistance(best.candidate.bpm, match.bpm)
              ? entry
              : best,
          )
        : undefined;
      if (byLength) {
        chosen = byLength.candidate;
        source = 'length';
      } else {
        // 2. The candidate whose hit density looks like a breakbeat.
        const byOnsets = tied.filter((entry) => {
          const perBeat = onsets / entry.candidate.beats;
          return perBeat >= ONSETS_PER_BEAT_MIN && perBeat <= ONSETS_PER_BEAT_MAX;
        });
        if (byOnsets.length === 1) {
          chosen = byOnsets[0].candidate;
          source = 'onsets';
        } else {
          // 3. Whatever sits inside the preferred range.
          const inPreferred = tied.filter(
            (entry) =>
              entry.candidate.bpm >= PREFERRED_MIN_BPM && entry.candidate.bpm <= PREFERRED_MAX_BPM,
          );
          chosen = (inPreferred[0] ?? tied[0]).candidate;
          source = 'range';
        }
      }
    }

    const onsetsPerBeat = chosen.beats > 0 ? onsets / chosen.beats : null;
    // A name or a length match is evidence in itself; the weaker rules, and a
    // tempo the estimate cannot agree with, are worth a listen.
    const flagged =
      source === 'onsets' || source === 'range' || !tempoAgrees(chosen.bpm, approximateBpm);

    return { ...base, bpm: chosen.bpm, beats: chosen.beats, source, onsetsPerBeat, flagged };
  });
}

export function bpmSourceLabel(source: BpmSource): string {
  switch (source) {
    case 'name':
      return 'from name';
    case 'length':
      return 'matched by length';
    case 'onsets':
      return 'onset density';
    case 'range':
      return 'tempo range';
    default:
      return 'duration + analysis';
  }
}
