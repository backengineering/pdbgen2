// Copyright (C) Back Engineering Labs, Inc. - All Rights Reserved
//

#ifndef UTILS_H
#define UTILS_H

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
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>

#include <Windows.h>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

struct ModuleInfo {
  std::vector<llvm::object::coff_section> sections;
  llvm::codeview::GUID guid;
  uint32_t age;
  uint32_t signature;
};

struct Entry {
  uint32_t rangeStart;
  uint32_t rangeEnd;
  uint32_t original;
};

struct SectionAndOffset {
  uint32_t sectionNumber;
  uint32_t sectionOffset;
};

inline llvm::ExitOnError exitOnErr;

// https://github.com/gix/PdbGen/blob/568d23b671eda39d7bc562e511e8dda4b18aa18b/Main.cpp#L37
llvm::Error readModuleInfo(llvm::StringRef modulePath, ModuleInfo &info);
std::vector<Entry> parseEntriesFromFile(const std::string &filePath);
llvm::Expected<SectionAndOffset>
rvaToSectionAndOffset(std::uint32_t rva,
                      const std::vector<llvm::object::coff_section> &sections);

#endif // UTILS_H