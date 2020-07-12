/* PDB support for BFD. */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

#include <libr/r_pdb.h>
#include "pdb-types.h"

typedef struct pdb_data_struct {
  R_PDB *pdb;
} bfd_pdb_data_struct;
