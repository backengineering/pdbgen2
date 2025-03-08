// Copyright (C) Back Engineering Labs, Inc. - All Rights Reserved
// https://www.youtube.com/watch?v=gxmXWXUvNr8

#include <llvm/DebugInfo/CodeView/StringsAndChecksums.h>
#include <llvm/DebugInfo/CodeView/SymbolRecord.h>
#include <llvm/DebugInfo/MSF/MSFBuilder.h>
#include <llvm/DebugInfo/PDB/IPDBSession.h>
#include <llvm/DebugInfo/PDB/Native/DbiModuleDescriptorBuilder.h>
#include <llvm/DebugInfo/PDB/Native/DbiStream.h>
#include <llvm/DebugInfo/PDB/Native/DbiStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/GSIStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/InfoStream.h>
#include <llvm/DebugInfo/PDB/Native/InfoStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/NativeSession.h>
#include <llvm/DebugInfo/PDB/Native/PDBFile.h>
#include <llvm/DebugInfo/PDB/Native/PDBFileBuilder.h>
#include <llvm/DebugInfo/PDB/Native/SymbolStream.h>
#include <llvm/DebugInfo/PDB/Native/TpiStream.h>
#include <llvm/DebugInfo/PDB/Native/TpiStreamBuilder.h>
#include <llvm/DebugInfo/PDB/PDB.h>
#include <llvm/Object/Binary.h>
#include <llvm/Object/COFF.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

#include <Windows.h>
#include <charconv>
#include <fstream>

using namespace llvm;
using namespace llvm::pdb;
using namespace llvm::codeview;

static cl::opt<std::string>
    ObfuscatedPE("obf-pe", cl::desc("Path to the obfuscated PE file"),
                 cl::Required);

static cl::opt<std::string>
    MapFile("map", cl::desc("Path to the generated map file from CodeDefender"),
            cl::Required);

static cl::opt<std::string>
    OriginalPDB("orig-pdb", cl::desc("Path to the original PDB file"),
                cl::Required);

static cl::opt<std::string>
    OutputPDB("out-pdb", cl::desc("Path to the output PDB file"), cl::Required);

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(
      argc, argv,
      "CodeDefender PDB Generator\n- Made with love by CR3Swapper :)\n");

  llvm::ExitOnError ExitOnErr;
  llvm::BumpPtrAllocator alloc;
  llvm::pdb::PDBFileBuilder builder(alloc);
  const auto moduleName = "CodeDefender.obj";

  // Read the file off the disk and create a PDBFile.
  std::unique_ptr<IPDBSession> session;
  ExitOnErr(loadDataForPDB(PDB_ReaderType::Native, OriginalPDB, session));
  NativeSession *NS = static_cast<NativeSession *>(session.get());
  auto &pdb = NS->getPDBFile();

  ExitOnErr(builder.initialize(pdb.getBlockSize()));

  // Add each of the reserved streams.  We might not put any data in them,
  // but at least they have to be present.
  for (uint32_t i = 0; i < llvm::pdb::kSpecialStreamCount; ++i)
    ExitOnErr(builder.getMsfBuilder().addStream(0));

  InfoStream &info = pdb.getPDBInfoStream().get();
  InfoStreamBuilder &infoBuilder = builder.getInfoBuilder();

  infoBuilder.setVersion(info.getVersion());
  infoBuilder.setSignature(info.getSignature());
  infoBuilder.setAge(info.getAge());
  infoBuilder.setGuid(info.getGuid());
  infoBuilder.addFeature(llvm::pdb::PdbRaw_FeatureSig::VC140);

  DbiStream &dbi = pdb.getPDBDbiStream().get();
  DbiStreamBuilder &dbiBuilder = builder.getDbiBuilder();
  llvm::ArrayRef<llvm::object::coff_section> sections(
      std::vector<llvm::object::coff_section>{dbi.getSectionHeaders().begin(),
                                              dbi.getSectionHeaders().end()});

  dbiBuilder.setAge(dbi.getAge());
  dbiBuilder.setBuildNumber(dbi.getBuildNumber());
  dbiBuilder.setFlags(dbi.getFlags());
  dbiBuilder.setMachineType(dbi.getMachineType());
  dbiBuilder.setPdbDllRbld(1);
  dbiBuilder.setPdbDllVersion(1);
  dbiBuilder.setVersionHeader(PdbDbiV70);
  dbiBuilder.createSectionMap(sections);
  ExitOnErr(dbiBuilder.addDbgStream(
      DbgHeaderType::SectionHdr,
      {reinterpret_cast<uint8_t const *>(sections.data()),
       sections.size() * sizeof(sections[0])}));

  // Create a new single module named "CodeDefender.obj"
  auto &modiBuilder = ExitOnErr(dbiBuilder.addModuleInfo(moduleName));
  modiBuilder.setObjFileName(moduleName);

  // Copy types used from the original binary?
  TpiStream &tpi = pdb.getPDBTpiStream().get();
  TpiStreamBuilder &tpiBuilder = builder.getTpiBuilder();
  tpiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);
  for (auto idx = tpi.TypeIndexBegin(); idx != tpi.TypeIndexEnd(); idx++) {
    CVType ty = tpi.getType(TypeIndex(idx));
    tpiBuilder.addTypeRecord(ty.RecordData, std::nullopt);
  }

  // Copy types from the original binary?
  TpiStream &ipi = pdb.getPDBIpiStream().get();
  auto &ipiBuilder = builder.getIpiBuilder();
  ipiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);
  for (auto idx = ipi.TypeIndexBegin(); idx != ipi.TypeIndexEnd(); idx++) {
    CVType ty = ipi.getType(TypeIndex(idx));
    ipiBuilder.addTypeRecord(ty.RecordData, std::nullopt);
  }

  GSIStreamBuilder &gsiBuilder = builder.getGsiBuilder();
  SymbolStream &symstream = pdb.getPDBSymbolStream().get();

  bool hadError = false;
  for (auto &sym : symstream.getSymbols(&hadError)) {
    gsiBuilder.addGlobalSymbol(sym);
  }

  codeview::GUID ignored;
  ExitOnErr(builder.commit(OutputPDB, &ignored));
}