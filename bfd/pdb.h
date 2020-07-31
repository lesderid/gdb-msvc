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
#include <llvm/DebugInfo/PDB/IPDBSectionContrib.h>
#include <llvm/DebugInfo/PDB/PDBSymbolCompiland.h>
#include <llvm/DebugInfo/PDB/IPDBTable.h>
#include <llvm/Object/COFF.h>

typedef struct pdb_data_struct {
    std::unique_ptr<llvm::pdb::NativeSession> session;
} bfd_pdb_data_struct;
