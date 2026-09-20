import { describe, expect, it } from 'vitest';
import { BANK_HEADER_SIZE, BANK_MAX_SAMPLES, BANK_VERSION } from './bank';
import {
  PULSE_PPQN_VALUES,
  hasClockSync,
  hasExternalClockSettings,
  isCompatibleFirmware,
  parseClockDiagnostics,
  parseInfo,
  pulsePpqnCommand,
  restartOnStartCommand,
  shouldPollClockDiagnostics,
} from './serial';

const baseInfo = 'PIKO1 FW 2.2 F 2097152 R 524288 S 520192 A 524288 C 1560576 U 0 SR 24000 N 0';

describe('serial metadata parsing', () => {
  it('parses firmware compatibility tokens', () => {
    const info = parseInfo(
      `${baseInfo} PROTO 1 BANK_VERSION ${BANK_VERSION} BANK_HEADER_SIZE ${BANK_HEADER_SIZE} BANK_MAX_SAMPLES ${BANK_MAX_SAMPLES}\nEND\n`,
    );

    expect(info.firmware).toBe('2.2');
    expect(info.protocolVersion).toBe(1);
    expect(info.bankVersion).toBe(BANK_VERSION);
    expect(info.bankHeaderSize).toBe(BANK_HEADER_SIZE);
    expect(info.bankMaxSamples).toBe(BANK_MAX_SAMPLES);
  });

  it('parses clock sync, PPQN and restart metadata', () => {
    const info = parseInfo(
      `${baseInfo.replace('FW 2.2', 'FW 2.4')} CLOCK_SYNC_VERSION 2 PULSE_PPQN 24 RESTART_ON_START 1 PROTO 1 BANK_VERSION ${BANK_VERSION} BANK_HEADER_SIZE ${BANK_HEADER_SIZE} BANK_MAX_SAMPLES ${BANK_MAX_SAMPLES}\nEND\n`,
    );
    expect(info.clockSyncVersion).toBe(2);
    expect(info.pulsePpqn).toBe(24);
    expect(info.restartOnStart).toBe(true);
    expect(hasClockSync(info)).toBe(true);
  });

  it('reports the clock source and hides follower settings on the master build', () => {
    // The master build reports neither follower setting.
    const master = parseInfo(`${baseInfo} CLOCK_SYNC_VERSION 2 CLOCK_SOURCE INTERNAL\nEND\n`);
    expect(master.clockSource).toBe('INTERNAL');
    expect(hasClockSync(master)).toBe(true);
    expect(hasExternalClockSettings(master)).toBe(false);
    expect(master.pulsePpqn).toBeUndefined();
    expect(master.restartOnStart).toBeUndefined();

    const follower = parseInfo(
      `${baseInfo} CLOCK_SYNC_VERSION 2 CLOCK_SOURCE EXTERNAL PULSE_PPQN 24 RESTART_ON_START 1\nEND\n`,
    );
    expect(follower.clockSource).toBe('EXTERNAL');
    expect(hasExternalClockSettings(follower)).toBe(true);
  });

  it('parses a disabled restart-on-start setting', () => {
    const info = parseInfo(`${baseInfo} CLOCK_SYNC_VERSION 2 PULSE_PPQN 48 RESTART_ON_START 0\nEND\n`);
    expect(info.pulsePpqn).toBe(48);
    expect(info.restartOnStart).toBe(false);
  });

  it('feature-detects older firmware without changing compatibility', () => {
    const info = parseInfo(
      `${baseInfo} PROTO 1 BANK_VERSION ${BANK_VERSION} BANK_HEADER_SIZE ${BANK_HEADER_SIZE} BANK_MAX_SAMPLES ${BANK_MAX_SAMPLES}\nEND\n`,
    );
    expect(info.clockSyncVersion).toBeUndefined();
    expect(info.pulsePpqn).toBeUndefined();
    expect(info.restartOnStart).toBeUndefined();
    expect(hasClockSync(info)).toBe(false);
  });
});

describe('clock diagnostics', () => {
  it('parses the length-prefixed payload body tokens', () => {
    const diagnostics = parseClockDiagnostics(
      'CLOCK1 SOURCE PULSE STATE RUNNING BPM_X100 12000 JITTER_US 12 LAST_EDGE_AGE_US 100 PPQN 24 ACCEPTED 20 REJECTED 1 RESTART_COUNT 2 RESET_COUNT 3 CLOCK_QUEUE_DROPS 0 MIDI_QUEUE_DROPS 0\nEND\n',
    );
    expect(diagnostics.source).toBe('PULSE');
    expect(diagnostics.state).toBe('RUNNING');
    expect(diagnostics.bpmX100).toBe(12000);
    expect(diagnostics.ppqn).toBe(24);
    expect(diagnostics.rejected).toBe(1);
    expect(diagnostics.restartCount).toBe(2);
    expect(diagnostics.resetCount).toBe(3);
    expect(diagnostics.clockQueueDrops).toBe(0);
  });

  it('parses a stopped clock', () => {
    const diagnostics = parseClockDiagnostics(
      'CLOCK1 SOURCE PULSE STATE STOPPED BPM_X100 0 JITTER_US 0 LAST_EDGE_AGE_US 0 PPQN 24 ACCEPTED 0 REJECTED 0 RESTART_COUNT 0 RESET_COUNT 0 CLOCK_QUEUE_DROPS 0 MIDI_QUEUE_DROPS 0\nEND\n',
    );
    expect(diagnostics.state).toBe('STOPPED');
  });

  it('rejects malformed diagnostics', () => {
    expect(() => parseClockDiagnostics('CLOCK0 STATE RUNNING')).toThrow('Bad clock diagnostics');
  });
});

describe('pulse PPQN command', () => {
  it('encodes every supported PPQN setting', () => {
    expect(PULSE_PPQN_VALUES).toEqual([1, 2, 4, 8, 12, 24, 48]);
    for (const ppqn of PULSE_PPQN_VALUES) {
      expect(Array.from(pulsePpqnCommand(ppqn))).toEqual([0x50, ppqn]);
    }
  });

  it('rejects unsupported PPQN settings', () => {
    expect(() => pulsePpqnCommand(3)).toThrow('Invalid pulse PPQN');
    expect(() => pulsePpqnCommand(16)).toThrow('Invalid pulse PPQN');
  });
});

describe('restart-on-start command', () => {
  it('encodes both states', () => {
    expect(Array.from(restartOnStartCommand(true))).toEqual([0x54, 1]);
    expect(Array.from(restartOnStartCommand(false))).toEqual([0x54, 0]);
  });
});

describe('diagnostic polling suspension', () => {
  const capable = parseInfo(
    `${baseInfo} CLOCK_SYNC_VERSION 2 PULSE_PPQN 24 RESTART_ON_START 1 PROTO 1 BANK_VERSION ${BANK_VERSION} BANK_HEADER_SIZE ${BANK_HEADER_SIZE} BANK_MAX_SAMPLES ${BANK_MAX_SAMPLES}\nEND\n`,
  );

  it('polls only when connected, capable, idle, and without a request in flight', () => {
    expect(shouldPollClockDiagnostics(true, false, capable, false)).toBe(true);
    expect(shouldPollClockDiagnostics(true, true, capable, false)).toBe(false);
    expect(shouldPollClockDiagnostics(true, false, capable, true)).toBe(false);
    expect(shouldPollClockDiagnostics(false, false, capable, false)).toBe(false);
    expect(shouldPollClockDiagnostics(true, false, parseInfo(baseInfo), false)).toBe(false);
  });
});

describe('firmware compatibility', () => {
  it('treats missing metadata as incompatible', () => {
    expect(isCompatibleFirmware(parseInfo(baseInfo))).toBe(false);
  });

  it('treats FW 2.1 without compatibility tokens as incompatible', () => {
    expect(isCompatibleFirmware(parseInfo(baseInfo.replace('FW 2.2', 'FW 2.1')))).toBe(false);
  });

  it('accepts matching v2 bank metadata', () => {
    const info = parseInfo(
      `${baseInfo} PROTO 1 BANK_VERSION ${BANK_VERSION} BANK_HEADER_SIZE ${BANK_HEADER_SIZE} BANK_MAX_SAMPLES ${BANK_MAX_SAMPLES}\nEND\n`,
    );

    expect(isCompatibleFirmware(info)).toBe(true);
  });

  it('rejects wrong bank version', () => {
    const info = parseInfo(`${baseInfo} PROTO 1 BANK_VERSION 1 BANK_HEADER_SIZE ${BANK_HEADER_SIZE} BANK_MAX_SAMPLES ${BANK_MAX_SAMPLES}\nEND\n`);

    expect(isCompatibleFirmware(info)).toBe(false);
  });

  it('rejects wrong header size', () => {
    const info = parseInfo(`${baseInfo} PROTO 1 BANK_VERSION ${BANK_VERSION} BANK_HEADER_SIZE 4096 BANK_MAX_SAMPLES ${BANK_MAX_SAMPLES}\nEND\n`);

    expect(isCompatibleFirmware(info)).toBe(false);
  });

  it('rejects insufficient max sample support', () => {
    const info = parseInfo(`${baseInfo} PROTO 1 BANK_VERSION ${BANK_VERSION} BANK_HEADER_SIZE ${BANK_HEADER_SIZE} BANK_MAX_SAMPLES 32\nEND\n`);

    expect(isCompatibleFirmware(info)).toBe(false);
  });
});
