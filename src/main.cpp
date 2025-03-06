#pragma warning(push)
#pragma warning(disable : 4141)
#pragma warning(disable : 4146)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4996)
#include <llvm/DebugInfo/CodeView/StringsAndChecksums.h>
#include <llvm/DebugInfo/CodeView/SymbolRecord.h>
#include <llvm/DebugInfo/MSF/MSFBuilder.h>
#include <llvm/DebugInfo/PDB/Native/DbiModuleDescriptorBuilder.h>
#include <llvm/DebugInfo/PDB/Native/DbiStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/GSIStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/InfoStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/PDBFileBuilder.h>
#include <llvm/DebugInfo/PDB/Native/TpiStreamBuilder.h>
#include <llvm/Object/Binary.h>
#include <llvm/Object/COFF.h>
#include <llvm/Support/Allocator.h>
#pragma warning(pop)

#include <charconv>
#include <fstream>

int main(int argc, char **argv) {
  llvm::ExitOnError ExitOnErr;
  llvm::BumpPtrAllocator alloc;
  llvm::pdb::PDBFileBuilder builder(alloc);

  uint32_t const blockSize = 4096;
  ExitOnErr(builder.initialize(blockSize));

  // Add each of the reserved streams.  We might not put any data in them,
  // but at least they have to be present.
  for (uint32_t i = 0; i < llvm::pdb::kSpecialStreamCount; ++i)
    ExitOnErr(builder.getMsfBuilder().addStream(0));

  auto &infoBuilder = builder.getInfoBuilder();

  infoBuilder.setAge(1);
  llvm::codeview::GUID guid;

  infoBuilder.setGuid(guid);
  infoBuilder.setSignature(1);
  infoBuilder.setVersion(llvm::pdb::PdbImplVC70);
  infoBuilder.addFeature(llvm::pdb::PdbRaw_FeatureSig::VC140);

  auto &dbiBuilder = builder.getDbiBuilder();
  dbiBuilder.setAge(1);
  dbiBuilder.setBuildNumber(35584);
  dbiBuilder.setFlags(2);
  dbiBuilder.setMachineType(llvm::pdb::PDB_Machine::Amd64);
  dbiBuilder.setPdbDllRbld(1);
  dbiBuilder.setPdbDllVersion(1);
  dbiBuilder.setVersionHeader(llvm::pdb::PdbDbiV70);
  // dbiBuilder.createSectionMap(moduleInfo.sections);

  //ExitOnErr(dbiBuilder.addDbgStream(
  //    llvm::pdb::DbgHeaderType::SectionHdr,
  //    {reinterpret_cast<uint8_t const *>(moduleInfo.sections.data()),
  //     moduleInfo.sections.size() * sizeof(moduleInfo.sections[0])}));

  auto &modiBuilder = ExitOnErr(dbiBuilder.addModuleInfo("fake-pdb.obj"));
  modiBuilder.setObjFileName("fake-pdb.obj");
  auto &gsiBuilder = builder.getGsiBuilder();

  auto &tpiBuilder = builder.getTpiBuilder();
  tpiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);

  auto &ipiBuilder = builder.getIpiBuilder();
  ipiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);

  llvm::codeview::StringsAndChecksums strings;
  strings.setStrings(
      std::make_shared<llvm::codeview::DebugStringTableSubsection>());
  strings.strings()->insert("");
  builder.getStringTableBuilder().setStrings(*strings.strings());

  dbiBuilder.setPublicsStreamIndex(gsiBuilder.getPublicsStreamIndex());
  ExitOnErr(builder.commit("output.pdb", &infoBuilder.getGuid()));
}