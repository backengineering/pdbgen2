# pdbgen

Generate a PDB file given the old PDB file and an address mapping file generated from CodeDefender. Not every tool that parses PDB files supports [PDB OMAP](https://github.com/getsentry/pdb/issues/17) therefore partial reconstruction of the PDB is required to support all tools (`IDA`, `WinDbg`, `x64dbg`, `Visual Studios`).

## Precompiled

You can download a pre-compiled version of this project instead of having to build this entire project. Head over to the github releases tab.

## Building

This will generate cmake `build` folder. You can then go into `build/` and open `pdbgen2.sln`.

```
cmake -B build
```