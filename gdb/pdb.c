#include "pdb.h"

extern const bfd_target pdb_vec; //from targets.c

#include "objfiles.h"
#include "gdb/fileio.h"

#include "gdbsupport/pathstuff.h"
#include <regex>
#include <fstream>
#include <cassert>

extern "C" {
#include <coff/internal.h>
#include <coff/x86_64.h> //this is really dumb but oh well
#include <coff/pe.h>
#include <libcoff.h>
#include <libpei.h>
}

#include "psympriv.h"

static gdb::optional<std::string>
get_codeview_pdb_path (objfile *objfile)
{
  gdb::optional<std::string> nullopt;

  bfd *abfd = objfile->obfd;

  pe_data_type *pe = pe_data (abfd);
  struct internal_extra_pe_aouthdr *extra = &pe->pe_opthdr;

  bfd_size_type size = extra->DataDirectory[PE_DEBUG_DATA].Size;
  if (size == 0)
    return nullopt;
  bfd_vma addr = extra->DataDirectory[PE_DEBUG_DATA].VirtualAddress + extra->ImageBase;

  asection *section;
  for (section = abfd->sections; section != nullptr; section = section->next)
    {
      if ((addr >= section->vma) && (addr < (section->vma + section->size)))
        break;
    }
  if (section == nullptr || !(section->flags & SEC_HAS_CONTENTS) || section->size < size)
    return nullopt;

  bfd_size_type data_offset;
  data_offset = addr - section->vma;
  if (size > (section->size - data_offset))
    return nullopt;

  bfd_byte *data = nullptr;
  if (!bfd_malloc_and_get_section (abfd, section, &data))
    return nullopt;

  for (auto i = 0; i < size / sizeof (struct external_IMAGE_DEBUG_DIRECTORY); i++)
    {
      struct external_IMAGE_DEBUG_DIRECTORY *ext
        = &((struct external_IMAGE_DEBUG_DIRECTORY *) (data + data_offset))[i];
      struct internal_IMAGE_DEBUG_DIRECTORY idd{};

      _bfd_pei_swap_debugdir_in (abfd, ext, &idd);

      if (idd.Type == PE_IMAGE_DEBUG_TYPE_CODEVIEW)
        {
          char buffer[256 + 1] ATTRIBUTE_ALIGNED_ALIGNOF (CODEVIEW_INFO);
          auto *cvinfo = (CODEVIEW_INFO *) buffer;

          if (!_bfd_pei_slurp_codeview_record (abfd, (file_ptr) idd.PointerToRawData,
                                               idd.SizeOfData, cvinfo))
            continue;

          return std::string (cvinfo->PdbFileName);
        }
    }

  return nullopt;
}

static std::vector<std::string>
get_pdb_paths (struct objfile *objfile)
{
  std::vector<std::string> paths;

  auto codeview_pdb_path = get_codeview_pdb_path (objfile);
  if (!codeview_pdb_path)
    return paths; //if there is no CodeView PDB path, we assume no PDB exists

  //TODO: Only push_back 'target:' paths if we have a remote target

  paths.push_back (*codeview_pdb_path);
  paths.push_back ("target:" + *codeview_pdb_path);

  auto real_path = gdb_realpath (objfile->original_name);
  auto naive_pdb_path = std::regex_replace (std::string (real_path.get ()),
                                            std::regex ("\\.[^.]*$"),
                                            ".pdb");
  naive_pdb_path = std::regex_replace (naive_pdb_path,
                                       std::regex ("target:"),
                                       "");

  paths.push_back (naive_pdb_path);
  paths.push_back ("target:" + naive_pdb_path);

  return paths;
}

gdb_bfd_ref_ptr
try_load_pdb_bfd (objfile *objfile)
{
  auto paths = get_pdb_paths (objfile);

  if (paths.empty ()) return nullptr;

  for (auto & path : paths)
    {
      try
        {
          gdb_bfd_ref_ptr debug_bfd (symfile_bfd_open (path.c_str ()));
          if (debug_bfd.get () && debug_bfd.get ()->xvec == &pdb_vec)
            {
              return debug_bfd;
            }
        }
      catch (gdb_exception_error & error)
        {
          continue;
        }
    }

  return nullptr;
}

static void
pdb_sym_new_init (objfile *objfile)
{
}

static void
pdb_sym_init (objfile *objfile)
{
}

static void
pdb_sym_read (objfile *objfile, symfile_add_flags symfile_flags)
{
  gdb::def_vector<asymbol *> symbol_table;

  bfd *abfd = objfile->obfd;

  auto& pdbFile = bfd_pdb_get_llvm_pdb_file(abfd);
  (void) pdbFile;
  //TODO: load symbols
  return;

  fail:
  error (_("Can't read symbols from %s: %s"),
         bfd_get_filename (objfile->obfd),
         bfd_errmsg (bfd_get_error ()));
}

static void
pdb_sym_finish (objfile *objfile)
{
}

const struct sym_fns pdb_sym_fns = {
  pdb_sym_new_init,
  pdb_sym_init,
  pdb_sym_read,
  nullptr, /* sym_read_psymbols */
  pdb_sym_finish,
  default_symfile_offsets,
  default_symfile_segments,
  nullptr, /* sym_read_linetable */
  default_symfile_relocate,
  nullptr, /* sym_probe_fns */
  &psym_functions
};

void
_initialize_pdb ()
{
  add_symtab_fns (bfd_target_pdb_flavour, &pdb_sym_fns);
}