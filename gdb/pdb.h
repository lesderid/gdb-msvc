#include "defs.h"

#include <llvm/Object/COFF.h>
#include <llvm/DebugInfo/PDB/Native/PDBFile.h>
#undef __STDC_CONSTANT_MACROS
#undef __STDC_LIMIT_MACROS

#include "symtab.h"
#include "minsyms.h"
#include "gdb_bfd.h"

extern gdb_bfd_ref_ptr
try_load_pdb_bfd (objfile *objfile);

//HACK: bfd imports:
extern const llvm::object::coff_section &
bfd_pdb_get_llvm_section_header (asection *section);

extern llvm::pdb::PDBFile &
bfd_pdb_get_llvm_pdb_file (bfd *abfd);
