#include <utils.h>

using namespace llvm;
using namespace llvm::pdb;
using namespace llvm::codeview;

static cl::opt<std::string>
    mapFilePath("map-file",
                cl::desc("Map file produced by a CodeDefender product"),
                cl::Required);

static cl::opt<std::string>
    obfuscatedPE("obf-pe", cl::desc("Path to the obfuscated PE file"),
                 cl::Required);

static cl::opt<std::string>
    outputPDB("out-pdb", cl::desc("Path to the output PDB file"), cl::Required);

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(
      argc, argv,
      "CodeDefender PDB Generator\n- Made with love by CR3Swapper :)\n");

  std::vector<Entry> entries = parseEntriesFromFile(mapFilePath);
  llvm::BumpPtrAllocator alloc;
  llvm::pdb::PDBFileBuilder builder(alloc);
  ModuleInfo moduleInfo;

  exitOnErr(readModuleInfo(obfuscatedPE, moduleInfo));
  exitOnErr(builder.initialize(4096));

  for (uint32_t i = 0; i < llvm::pdb::kSpecialStreamCount; ++i)
    exitOnErr(builder.getMsfBuilder().addStream(0));

  InfoStreamBuilder &infoBuilder = builder.getInfoBuilder();
  infoBuilder.setSignature(moduleInfo.signature);
  infoBuilder.setAge(moduleInfo.age);
  infoBuilder.setGuid(moduleInfo.guid);
  infoBuilder.setHashPDBContentsToGUID(false);
  infoBuilder.setVersion(llvm::pdb::PdbImplVC70);
  infoBuilder.addFeature(llvm::pdb::PdbRaw_FeatureSig::VC140);

  DbiStreamBuilder &dbiBuilder = builder.getDbiBuilder();
  std::vector<llvm::object::coff_section> sections{moduleInfo.sections.begin(),
                                                   moduleInfo.sections.end()};

  llvm::ArrayRef<llvm::object::coff_section> sectionsRef(sections);

  dbiBuilder.setAge(moduleInfo.age);
  dbiBuilder.setBuildNumber(14, 11);
  dbiBuilder.setMachineType(PDB_Machine::Amd64);
  dbiBuilder.setPdbDllRbld(1);
  dbiBuilder.setPdbDllVersion(1);
  dbiBuilder.setVersionHeader(PdbDbiV70);
  dbiBuilder.setFlags(DbiFlags::FlagStrippedMask);
  dbiBuilder.createSectionMap(sectionsRef);

  auto &tpiBuilder = builder.getTpiBuilder();
  tpiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);

  auto &ipiBuilder = builder.getIpiBuilder();
  ipiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);

  exitOnErr(dbiBuilder.addDbgStream(
      DbgHeaderType::SectionHdr,
      {reinterpret_cast<uint8_t const *>(sectionsRef.data()),
       sectionsRef.size() * sizeof(sectionsRef[0])}));

  auto &modiBuilder = exitOnErr(dbiBuilder.addModuleInfo("CodeDefender"));

  modiBuilder.setObjFileName("CodeDefender.obj");
  auto &gsiBuilder = builder.getGsiBuilder();

  std::vector<BulkPublic> pubs(entries.size());
  std::unordered_map<std::string, std::uint32_t> nameCounts;
  std::vector<std::string> names;

  for (const auto &entry : entries) {
    SectionAndOffset scnAndOffset =
        exitOnErr(rvaToSectionAndOffset(entry.rangeStart, sections));

    std::string baseName = llvm::formatv("ORIGINAL_{0:X}", entry.original);
    std::string name;

    auto countIt = nameCounts.find(baseName);
    if (countIt != nameCounts.end()) {
      std::uint32_t count = ++countIt->second;
      name = llvm::formatv("{0}_{1}", baseName, count);
    } else {
      nameCounts[baseName] = 0;
      name = baseName;
    }

    names.push_back(name);

    BulkPublic pub;
    pub.Name = names.back().c_str();
    pub.NameLen = name.size();
    pub.Segment = scnAndOffset.sectionNumber;
    pub.Offset = scnAndOffset.sectionOffset;
    pub.setFlags(PublicSymFlags::Code);
    pubs.push_back(pub);
  }
  gsiBuilder.addPublicSymbols(std::move(pubs));

  codeview::GUID ignored;
  exitOnErr(builder.commit(outputPDB, &ignored));
}
