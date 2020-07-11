#include "defs.h"
#include "symtab.h"
#include "minsyms.h"
#include "gdb_bfd.h"

extern void
read_pdb (struct objfile *objfile, minimal_symbol_reader & reader);

extern gdb_bfd_ref_ptr
try_load_pdb_bfd (objfile *objfile);
