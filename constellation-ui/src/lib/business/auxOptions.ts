import type { IoEntry } from "$lib/business/ioConfig";

export type Rule = {
  type: string,
  io: string,
  st: string,
  op: string,
  sen: string,
  diff: string,
  andOr: string,
  ref: string,
  first: boolean,
  sensorOption: string,
};

export class Auxiliary {
  InputConfig: string[] = [];
  OutputConfig: string[] = [];
  IoNames: IoEntry[] = [];
  systemMode: string = '';
  auxProg: string[] = [];
  rules: Rule[] = [];
}
