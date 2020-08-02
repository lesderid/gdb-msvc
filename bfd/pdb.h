/* PDB support for BFD. */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

#include <memory>
#include <iostream>
#include <llvm/Support/ErrorOr.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/BinaryStream.h>
#include <llvm/Support/Allocator.h>
#include <llvm/DebugInfo/PDB/Native/PDBFile.h>
#include <llvm/DebugInfo/PDB/Native/DbiStream.h>
#include <llvm/DebugInfo/PDB/Native/NativeSession.h>
#include <llvm/DebugInfo/PDB/Native/PublicsStream.h>
#include <llvm/DebugInfo/PDB/Native/GlobalsStream.h>
#include <llvm/DebugInfo/PDB/Native/SymbolStream.h>
#include <llvm/DebugInfo/PDB/IPDBSectionContrib.h>
#include <llvm/DebugInfo/PDB/PDBSymbolCompiland.h>
#include <llvm/DebugInfo/PDB/IPDBTable.h>
#include <llvm/DebugInfo/CodeView/CodeView.h>
#include <llvm/DebugInfo/CodeView/SymbolVisitorCallbackPipeline.h>
#include <llvm/DebugInfo/CodeView/SymbolDeserializer.h>
#include <llvm/DebugInfo/CodeView/CVSymbolVisitor.h>
#include <llvm/Object/COFF.h>

using namespace llvm::codeview;

typedef struct pdb_data_struct {
    std::unique_ptr<llvm::pdb::PDBFile> llvmPdbFile;
} bfd_pdb_data_struct;

