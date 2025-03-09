// Copyright (C) Back Engineering Labs, Inc. - All Rights Reserved
// https://www.youtube.com/watch?v=gxmXWXUvNr8

#include <llvm/DebugInfo/CodeView/AppendingTypeTableBuilder.h>
#include <llvm/DebugInfo/CodeView/ContinuationRecordBuilder.h>
#include <llvm/DebugInfo/CodeView/StringsAndChecksums.h>
#include <llvm/DebugInfo/CodeView/SymbolRecord.h>
#include <llvm/DebugInfo/CodeView/SymbolSerializer.h>
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
#include <llvm/DebugInfo/PDB/Native/TpiHashing.h>
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

llvm::ExitOnError ExitOnErr;

struct ModuleInfo {
  std::vector<llvm::object::coff_section> sections;
  llvm::codeview::GUID guid;
  uint32_t age;
  uint32_t signature;
};

static cl::opt<std::string>
    ObfuscatedPE("obf-pe", cl::desc("Path to the obfuscated PE file"),
                 cl::Required);

static cl::opt<std::string>
    MapFile("map", cl::desc("Path to the generated map file from CodeDefender"),
            cl::Required);

static cl::opt<std::string>
    OutputPDB("out-pdb", cl::desc("Path to the output PDB file"), cl::Required);

// https://github.com/gix/PdbGen/blob/568d23b671eda39d7bc562e511e8dda4b18aa18b/Main.cpp#L37
llvm::Error ReadModuleInfo(llvm::StringRef modulePath, ModuleInfo &info) {
  using namespace llvm;
  using namespace llvm::object;

  auto expectedBinary = createBinary(modulePath);
  if (!expectedBinary)
    return expectedBinary.takeError();

  OwningBinary<Binary> binary = std::move(*expectedBinary);

  if (binary.getBinary()->isCOFF()) {
    auto const obj = llvm::cast<COFFObjectFile>(binary.getBinary());
    for (auto const &sectionRef : obj->sections())
      info.sections.push_back(*obj->getCOFFSection(sectionRef));

    for (auto const &debugDir : obj->debug_directories()) {
      info.signature = debugDir.TimeDateStamp;
      if (debugDir.Type == IMAGE_DEBUG_TYPE_CODEVIEW) {
        DebugInfo const *debugInfo;
        StringRef pdbFileName;
        ExitOnErr(obj->getDebugPDBInfo(&debugDir, debugInfo, pdbFileName));

        switch (debugInfo->Signature.CVSignature) {
        case OMF::Signature::PDB70:
          info.age = debugInfo->PDB70.Age;
          std::memcpy(&info.guid, debugInfo->PDB70.Signature,
                      sizeof(info.guid));
          break;
        }
      }
    }

    return Error::success();
  }

  return errorCodeToError(std::make_error_code(std::errc::not_supported));
}

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(
      argc, argv,
      "CodeDefender PDB Generator\n- Made with love by CR3Swapper :)\n");

  llvm::BumpPtrAllocator alloc;
  llvm::pdb::PDBFileBuilder builder(alloc);
  ModuleInfo moduleInfo;

  ExitOnErr(ReadModuleInfo(ObfuscatedPE, moduleInfo));
  ExitOnErr(builder.initialize(4096));

  // Add each of the reserved streams.  We might not put any data in them,
  // but at least they have to be present.
  for (uint32_t i = 0; i < llvm::pdb::kSpecialStreamCount; ++i)
    ExitOnErr(builder.getMsfBuilder().addStream(0));

  InfoStreamBuilder &infoBuilder = builder.getInfoBuilder();
  infoBuilder.setSignature(moduleInfo.signature);
  infoBuilder.setAge(moduleInfo.age);
  infoBuilder.setGuid(moduleInfo.guid);
  infoBuilder.setHashPDBContentsToGUID(false);
  infoBuilder.setVersion(llvm::pdb::PdbImplVC70);
  infoBuilder.addFeature(llvm::pdb::PdbRaw_FeatureSig::VC140);

  DbiStreamBuilder &dbiBuilder = builder.getDbiBuilder();
  llvm::ArrayRef<llvm::object::coff_section> sections(
      std::vector<llvm::object::coff_section>{moduleInfo.sections.begin(),
                                              moduleInfo.sections.end()});

  dbiBuilder.setAge(moduleInfo.age);
  dbiBuilder.setBuildNumber(14, 11);
  dbiBuilder.setMachineType(PDB_Machine::Amd64);
  dbiBuilder.setPdbDllRbld(1);
  dbiBuilder.setPdbDllVersion(1);
  dbiBuilder.setVersionHeader(PdbDbiV70);
  dbiBuilder.setFlags(DbiFlags::FlagStrippedMask);
  dbiBuilder.createSectionMap(sections);

  auto &tpiBuilder = builder.getTpiBuilder();
  tpiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);

  auto &ipiBuilder = builder.getIpiBuilder();
  ipiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);

  ExitOnErr(dbiBuilder.addDbgStream(
      DbgHeaderType::SectionHdr,
      {reinterpret_cast<uint8_t const *>(sections.data()),
       sections.size() * sizeof(sections[0])}));

  auto &modiBuilder =
      ExitOnErr(dbiBuilder.addModuleInfo("C:\\CodeDefender.obj"));
  modiBuilder.setObjFileName("C:\\CodeDefender.obj");
  auto &gsiBuilder = builder.getGsiBuilder();

  ProcSym proc = ProcSym(SymbolRecordKind::GlobalProcSym);
  proc.Name = "HelloWorldTest";
  proc.Segment = 0;
  proc.CodeOffset = 0;
  proc.CodeSize = 5;

  modiBuilder.addSymbol(
      SymbolSerializer::writeOneSymbol(proc, alloc, CodeViewContainer::Pdb));

  codeview::GUID ignored;
  ExitOnErr(builder.commit(OutputPDB, &ignored));
}