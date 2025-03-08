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
#include <llvm/DebugInfo/PDB/Native/TpiStreamBuilder.h>
#include <llvm/DebugInfo/PDB/PDB.h>
#include <llvm/Object/Binary.h>
#include <llvm/Object/COFF.h>
#include <llvm/Support/Allocator.h>

#include <Windows.h>
#include <charconv>
#include <fstream>

using namespace llvm::pdb;

int main(int argc, char **argv) {
  llvm::ExitOnError ExitOnErr;
  llvm::BumpPtrAllocator alloc;
  llvm::pdb::PDBFileBuilder builder(alloc);

  // Add each of the reserved streams.  We might not put any data in them,
  // but at least they have to be present.
  for (uint32_t i = 0; i < llvm::pdb::kSpecialStreamCount; ++i)
    ExitOnErr(builder.getMsfBuilder().addStream(0));

  // Read the file off the disk and create a PDBFile.
  std::unique_ptr<IPDBSession> session;
  ExitOnErr(loadDataForPDB(PDB_ReaderType::Native, argv[1], session));
  NativeSession *NS = static_cast<NativeSession *>(session.get());
  auto &pdb = NS->getPDBFile();

  ExitOnErr(builder.initialize(pdb.getBlockSize()));
  InfoStream &info = pdb.getPDBInfoStream().get();
  InfoStreamBuilder &infoBuilder = builder.getInfoBuilder();

  infoBuilder.setVersion(info.getVersion());
  infoBuilder.setSignature(info.getSignature());
  infoBuilder.setAge(info.getAge());
  infoBuilder.setGuid(info.getGuid());
  infoBuilder.addFeature(llvm::pdb::PdbRaw_FeatureSig::VC140);

  DbiStream &dbi = pdb.getPDBDbiStream().get();
  DbiStreamBuilder &dbiBuilder = builder.getDbiBuilder();

  dbiBuilder.setAge(dbi.getAge());
  dbiBuilder.setBuildNumber(dbi.getBuildNumber());
  dbiBuilder.setFlags(dbi.getFlags());
  dbiBuilder.setMachineType(dbi.getMachineType());
  dbiBuilder.setPdbDllRbld(1);
  dbiBuilder.setPdbDllVersion(1);
  dbiBuilder.setVersionHeader(PdbDbiV70);

  GSIStreamBuilder &gsiBuilder = builder.getGsiBuilder();
}