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
  IoNames: string[] = [];
  systemMode: string = '';
  auxProg: string[] = [];
  rules: Rule[] = [];
}
