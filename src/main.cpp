// Copyright (C) Back Engineering Labs, Inc. - All Rights Reserved
//

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

static cl::opt<std::string>
    originalPDB("orig-pdb", cl::desc("Path to the original PDB file"),
                cl::Required);

// Helper function to create and append a public symbol.
void createAndAppendSymbol(std::vector<BulkPublic> &existingSymbols,
                           const char *symbolName, std::uint32_t segment,
                           std::uint32_t offset, PublicSymFlags flags) {
  BulkPublic pub;
  pub.Name = symbolName;
  pub.NameLen = strlen(symbolName);
  pub.Segment = segment;
  pub.Offset = offset;
  pub.setFlags(flags);
  existingSymbols.push_back(pub);
}

// Adjust name so that it is unique, remember that PDB files need unique
// symbol names because it uses a hash function.
void adjustName(std::string &symbolName,
                std::unordered_map<std::string, std::uint32_t> &nameCounts) {
  // Check for duplicates in nameCounts and modify if necessary
  auto countIt = nameCounts.find(symbolName);
  if (countIt != nameCounts.end()) {
    std::uint32_t count = ++countIt->second;
    symbolName = llvm::formatv("{0}_{1}", symbolName, count).str();
  } else {
    nameCounts[symbolName] = 0;
  }
}

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

  std::unique_ptr<IPDBSession> session;
  exitOnErr(loadDataForPDB(PDB_ReaderType::Native, originalPDB, session));
  NativeSession *NS = static_cast<NativeSession *>(session.get());
  auto &pdb = NS->getPDBFile();

  bool hadError = false;
  auto &symbolStream = exitOnErr(pdb.getPDBSymbolStream());
  auto &symbols = symbolStream.getSymbols(&hadError);
  std::vector<BulkPublic> originalSymbols;

  // This vector CANNOT get resized because i grab the raw c-string, and
  // remember... Small C++ strings are stored inside of the std::string class so
  // if the object moves my reference to that small string is now invalid...
  std::vector<std::string> names;
  names.reserve(entries.size() + std::distance(symbols.begin(), symbols.end()));
  std::unordered_map<std::string, std::uint32_t> nameCounts;

  // We want to keep data labels, functions, and code labels that point to the
  // original data in the obfuscated binary. Think the address of the jmp that
  // goes to the new function location We want to describe that address with a
  // name.
  for (const auto &sym : symbolStream.getSymbols(&hadError)) {
    switch (sym.kind()) {
    case SymbolKind::S_LPROC32:
    case SymbolKind::S_GPROC32:
    case SymbolKind::S_GPROC32_ID:
    case SymbolKind::S_LPROC32_ID:
    case SymbolKind::S_LPROC32_DPC:
    case SymbolKind::S_LPROC32_DPC_ID: {
      ProcSym pub = cantFail(SymbolDeserializer::deserializeAs<ProcSym>(sym));

      std::uint32_t rva =
          exitOnErr(sectionOffsetToRVA(pub.Segment, pub.CodeOffset, sections));

      if (!isAddressInRange(entries, rva)) {
        std::string symbolName = std::string(pub.Name);
        adjustName(symbolName, nameCounts);
        createAndAppendSymbol(originalSymbols,
                              names.emplace_back(symbolName).c_str(),
                              pub.Segment, pub.CodeOffset,
                              PublicSymFlags::Code | PublicSymFlags::Function);
      }
      break;
    }
    case SymbolKind::S_PUB32: {
      PublicSym32 pub =
          cantFail(SymbolDeserializer::deserializeAs<PublicSym32>(sym));

      std::uint32_t rva =
          exitOnErr(sectionOffsetToRVA(pub.Segment, pub.Offset, sections));

      if (!isAddressInRange(entries, rva)) {
        std::string symbolName = std::string(pub.Name);
        adjustName(symbolName, nameCounts);
        createAndAppendSymbol(originalSymbols,
                              names.emplace_back(symbolName).c_str(),
                              pub.Segment, pub.Offset,
                              PublicSymFlags::Code | PublicSymFlags::Function);
      }
      break;
    }
    case SymbolKind::S_LABEL32: {
      LabelSym pub = cantFail(SymbolDeserializer::deserializeAs<LabelSym>(sym));

      std::uint32_t rva =
          exitOnErr(sectionOffsetToRVA(pub.Segment, pub.CodeOffset, sections));

      if (!isAddressInRange(entries, rva)) {
        std::string symbolName = std::string(pub.Name);
        adjustName(symbolName, nameCounts);
        createAndAppendSymbol(
            originalSymbols, names.emplace_back(symbolName).c_str(),
            pub.Segment, pub.CodeOffset, (PublicSymFlags)pub.Flags);
      }
      break;
    }
    default:
      break;
    }
  }

  std::vector<BulkPublic> pubs(originalSymbols);
  pubs.resize(pubs.size() + entries.size());

  for (const auto &entry : entries) {
    SectionAndOffset scnAndOffset =
        exitOnErr(rvaToSectionAndOffset(entry.rangeStart, sections));

    auto sym = session->findSymbolByRVA(entry.original, PDB_SymType::None);
    std::string name;

    if (sym) {
      name = llvm::formatv("{}_RVA_{:X}", sym->getName(), entry.original);
    } else {
      // Fallback to a default naming scheme if no symbol is found
      name = llvm::formatv("ORIGINAL_{0:X}", entry.original).str();
    }

    adjustName(name, nameCounts);
    names.emplace_back(name);

    BulkPublic pub;
    pub.Name = names.back().c_str();
    pub.NameLen = name.size();
    pub.Segment = scnAndOffset.sectionNumber;
    pub.Offset = scnAndOffset.sectionOffset;
    pub.setFlags(PublicSymFlags::Code | PublicSymFlags::Function);
    pubs.push_back(pub);
  }
  gsiBuilder.addPublicSymbols(std::move(pubs));

  codeview::GUID ignored;
  exitOnErr(builder.commit(outputPDB, &ignored));
}
