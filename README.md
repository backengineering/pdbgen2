<div align="center">
    <img src="https://media1.giphy.com/media/FovFeej5SQQh94uyyK/giphy.gif"/>
    <br>
    <i>LLVM takes forever to build and a few GB's of cache space in tmp...</i>
</div>


Generate a PDB file given the old PDB file and an address mapping file generated from CodeDefender. Not every tool that parses PDB files supports [PDB OMAP](https://github.com/getsentry/pdb/issues/17) therefore partial reconstruction of the PDB is required to support all tools (`IDA`, `WinDbg`, `x64dbg`, `Visual Studios`).

## Precompiled

You can download a pre-compiled version of this project instead of having to build this entire project. Head over to the github releases tab.

## Building

This will generate cmake `build` folder. You can then go into `build/` and open `pdbgen2.sln`. You need to have `Visual Studios 2022` installed. It will take upwards of 30 minutes to configure, build, and install llvm.

```
cmake -B build -DLLVM_BUILD_TYPE=Release
# or
cmake -B build -DLLVM_BUILD_TYPE=Debug
```

Delete the `build/` folder if you wish to switch between `Release` and `Debug`, then re-run the above command.

## Credits

- [PdbGen](https://github.com/gix/PdbGen)
- [llvm-docs](https://llvm.org/docs/PDB/index.html)
- https://www.youtube.com/watch?v=gxmXWXUvNr8