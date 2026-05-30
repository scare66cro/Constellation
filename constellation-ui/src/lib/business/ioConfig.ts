/**
 * Structured I/O definition entry, emitted by the bridge from
 * msg 25 IoDefinition. Replaces the legacy CSV format
 * `Name:Mode:IoType:Renamable:Index`.
 *
 * Field semantics (mirrors `Settings.EquipIo[i]` in firmware):
 *   - mode    = SYSTEM_MODE category (0=potato, 1=onion, 2=both, etc.)
 *   - ioType  = IO_OPTION enum (0=OUTPUT, 1=INPUT, 2=BOTH, 3=SWITCH, 4=NONE)
 *   - ioPin   = physical port number (0xFFFFFFFF = unassigned)
 */
export type IoEntry = {
  index: number;
  name: string;
  mode: number;
  ioPin: number;
  renamable: boolean;
  visible: boolean;
  ioType: number;
};

export type IOConfigType = {
  ioAvailable: string[],
  config: {
    outputConfig: string[],
    inputConfig: string[],
  },
  ioNames: IoEntry[],
  systemMode: string,
}
