/* Includes code from radare2:
   radare - LGPL - Copyright 2014 - inisider
   (https://github.com/radareorg/radare2)  */

/*

HACK: Honestly, this whole thing is a hack:

1. radare's PDB functions aren't great for external use,
2. few if any edge cases work,
3. we pretend our PDB symbols are minisyms, and
4. no tests.

*/

#include "pdb.h"

extern const bfd_target pdb_vec; //from targets.c

//BEGIN HACK (conflicting symbols)
#undef QUIT

#include <libr/r_pdb.h>

#undef QUIT
#define QUIT maybe_quit ()
#undef B_SET
//END HACK (conflicting symbols)

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

#include "bfd/pdb-types.h"

struct find_section_by_name_args {
  const char *name;
  asection **resultp;
};

static void
find_section_by_name_filter (bfd *abfd, asection *sect, void *obj)
{
  (void) abfd;

  auto *args = (struct find_section_by_name_args *) obj;

  if (strcmp (sect->name, args->name) == 0)
    *args->resultp = sect;
}

static struct bfd_section *
section_by_name (const char *name, struct objfile *objfile)
{
  asection *sect = nullptr;
  struct find_section_by_name_args args{};
  args.name = name;
  args.resultp = &sect;
  bfd_map_over_sections (objfile->obfd, find_section_by_name_filter, &args);
  return sect;
}

static std::unique_ptr<RPdb>
get_r_pdb (const std::string & path)
{
  RPdb pdb = {nullptr};
  if (std::ifstream (path).good ())
    {
      if (init_pdb_parser (&pdb, path.c_str ()))
        return std::make_unique<RPdb> (pdb);
    }
  else if (path.rfind ("target:", 0) == 0)
    {
      auto target = find_target_at (process_stratum);
      if (!target)
        {
          return nullptr;
        }

      void *buffer;
      size_t length;

      { //begin read PDB into buffer
        int errno;

        auto fd = target->fileio_open (nullptr,
                                       path.c_str () + sizeof ("target:") - 1,
                                       FILEIO_O_RDONLY,
                                       0 /* mode */,
                                       true /* warn_if_slow */,
                                       &errno);

        if (fd == -1)
          {
            return nullptr;
          }

        struct stat st{};
        if (target->fileio_fstat (fd, &st, &errno) == -1)
          {
            return nullptr;
          }

        length = st.st_size;
        buffer = xmalloc (length);

        file_ptr pos = 0, bytes;
        while (length > pos)
          {
            bytes = target->fileio_pread (fd,
                                          (gdb_byte *) buffer + pos,
                                          length - pos,
                                          pos,
                                          &errno);
            if (bytes == 0)
              {
                break;
              }
            else if (bytes == -1)
              {
                xfree (buffer);
                return nullptr;
              }
            else
              {
                pos += bytes;
              }
          }
        assert (pos == length);

        target->fileio_close (fd, &errno);
      } //end read PDB into buffer

      auto r_buffer = r_buf_new_with_bytes ((const ut8 *) buffer, length);
      if (init_pdb_parser_with_buf (&pdb, r_buffer))
        {
          xfree (buffer);
          return std::make_unique<RPdb> (pdb);
        }

      xfree (buffer);
    }
  return nullptr;
}

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

static std::tuple<std::unique_ptr<RPdb>, std::string>
load_pdb (objfile *objfile)
{
  auto paths = get_pdb_paths (objfile);

  if (paths.empty ()) return {nullptr, std::string ()};

  for (auto & path : paths)
    {
      auto p = get_r_pdb (path);
      if (p)
        return {std::move (p), path};
    }

  return {nullptr, std::string ()};
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

void
read_pdb (struct objfile *objfile, minimal_symbol_reader & reader)
{
  std::unique_ptr<RPdb> pdb;
  std::string pdb_path;
  std::tie (pdb, pdb_path) = load_pdb (objfile);

  if (!pdb)
    return;

  if (pdb->pdb_parse (pdb.get ()))
    {
      SStreamParseFunc *omap = nullptr, *sctns = nullptr, *sctns_orig = nullptr, *gsym = nullptr, *tmp;
      SIMAGE_SECTION_HEADER *sctn_header;
      SGDATAStream *gsym_data_stream;
      SPEStream *pe_stream = nullptr;
      SGlobal *gdata;
      RListIter *it;
      RList *l;

      l = pdb->pdb_streams2;
      it = r_list_iterator (l);
      while (r_list_iter_next (it))
        {
          tmp = (SStreamParseFunc *) r_list_iter_get (it);
          switch (tmp->type)
            {
              case ePDB_STREAM_SECT__HDR_ORIG:
                sctns_orig = tmp;
              break;
              case ePDB_STREAM_SECT_HDR:
                sctns = tmp;
              break;
              case ePDB_STREAM_OMAP_FROM_SRC:
                omap = tmp;
              break;
              case ePDB_STREAM_GSYM:
                gsym = tmp;
              break;
              default:
                break;
            }
        }
      if (!gsym)
        {
          eprintf ("There is no global symbols in current PDB.\n");
          return;
        }
      gsym_data_stream = (SGDATAStream *) gsym->stream;
      if ((omap != nullptr) && (sctns_orig != nullptr))
        {
          pe_stream = (SPEStream *) sctns_orig->stream;
        }
      else if (sctns)
        {
          pe_stream = (SPEStream *) sctns->stream;
        }
      if (!pe_stream)
        {
          return;
        }

      printf_filtered (_("Reading symbols from %s...\n"), pdb_path.c_str ());

      it = r_list_iterator (gsym_data_stream->globals_list);
      while (r_list_iter_next (it))
        {
          gdata = (SGlobal *) r_list_iter_get (it);
          sctn_header = (SIMAGE_SECTION_HEADER *) r_list_get_n (pe_stream->sections_hdrs,
                                                                gdata->segment - 1);
          if (sctn_header)
            {
              asection *sect = section_by_name (sctn_header->name, objfile);

              auto section = sect ? sect->index : -1;
              auto section_address = sect ? bfd_section_vma (sect) : 0;
              auto address = section_address + gdata->offset;
              if (address == 0)
                continue; //we don't want to record unresolved symbols or something?
              auto type = mst_text; //FIXME
              reader.record_with_info (gdata->name.name, address, type, section);
            }
        }
    }

  pdb->finish_pdb_parse (pdb.get ());
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
