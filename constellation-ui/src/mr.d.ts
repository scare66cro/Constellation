declare module 'miller-rabin' {
  import BN from 'bn.js';

  class MillerRabin {
    constructor(rand?: any);

    static create(rand?: any): MillerRabin;

    _randbelow(n: BN): BN;
    _randrange(start: BN, stop: BN): BN;
    test(n: BN, k?: number, cb?: (a: BN) => void): boolean;
    getDivisor(n: BN, k?: number): BN | false;
  }

  export = MillerRabin;
}