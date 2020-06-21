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

#include "defs.h"

//BEGIN HACK (conflicting symbols)
#undef QUIT

#include <libr/r_pdb.h>
#include <libr/r_core.h>

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
#include "psympriv.h"

//BEGIN radare2 imports
#include "pdb_types.h"

typedef void (*parse_stream_)(void *stream, R_STREAM_FILE *stream_file);

typedef struct
  {
    int indx;
    parse_stream_ parse_stream;
    void *stream;
    EStream type;
    free_func free;
  } SStreamParseFunc;
//END radare2 imports

static std::unique_ptr<R_PDB>
get_r_pdb (std::string path)
{
  R_PDB pdb = { 0 };
  if (std::ifstream (path).good ())
    {
      if (init_pdb_parser (&pdb, path.c_str ()))
        return std::make_unique<R_PDB> (pdb);
    }
  else if (path.rfind ("target:", 0) == 0)
    {
      auto target = find_target_at (process_stratum);

      void* buffer;
      size_t length;

        { //begin read PDB into buffer
          int errno;

          auto fd = target->fileio_open (nullptr,
                                         path.c_str () + sizeof ("target:") - 1,
                                         FILEIO_O_RDONLY, /* mode */ 0, /* warn_if_slow */ true, &errno);

          if (fd == -1)
            {
              return nullptr;
            }

          struct stat st;
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

      auto r_buffer = r_buf_new_with_bytes ((const ut8*) buffer, length);
      if (init_pdb_parser_with_buf (&pdb, r_buffer))
        {
          xfree (buffer);
          return std::make_unique<R_PDB> (pdb);
        }

      xfree (buffer);
    }
  return nullptr;
}

static std::string
get_pdb_path (struct objfile *objfile)
{
  /* HACK: Use _bfd_XXi_slurp_codeview_record to find the 'correct' PDB path.

     We should use this as fallback, but we shouldn't try to find a PDB if
     there is no codeview record at all.  */

  auto real_path = gdb_realpath (objfile->original_name);
  auto pdb_path = std::regex_replace (std::string (real_path.get ()),
                                      std::regex ("\\.[^.]*$"), ".pdb");
  return pdb_path;
}

struct find_section_by_name_args
{
  const char *name;
  asection **resultp;
};

static void
find_section_by_name_filter (bfd *abfd, asection *sect, void *obj)
{
  struct find_section_by_name_args *args = (struct find_section_by_name_args *) obj;

  if (strcmp (sect->name, args->name) == 0)
    *args->resultp = sect;
}

static struct bfd_section*
section_by_name (const char* name, struct objfile *objfile)
{
  asection *sect = NULL;
  struct find_section_by_name_args args;
  args.name = name;
  args.resultp = &sect;
  bfd_map_over_sections (objfile->obfd, find_section_by_name_filter, &args);
  return sect;
}

void
read_pdb (struct objfile *objfile, minimal_symbol_reader &reader)
{
  auto pdb_path = get_pdb_path (objfile);

  auto pdb = get_r_pdb (pdb_path);
  if (pdb)
    {
      if (pdb->pdb_parse (pdb.get ()))
        {
          SStreamParseFunc *omap = 0, *sctns = 0, *sctns_orig = 0, *gsym = 0, *tmp = 0;
          SIMAGE_SECTION_HEADER *sctn_header = 0;
          SGDATAStream *gsym_data_stream = 0;
          SPEStream *pe_stream = 0;
          SGlobal *gdata = 0;
          RListIter *it = 0;
          RList *l = 0;

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
          if ((omap != 0) && (sctns_orig != 0))
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

          printf_filtered (_("Reading symbols from %s...\n"), pdb_path.c_str());

          //TODO: Use this. For now, we allocate the symtab so we don't print we found no symbols.
          auto psymtab = allocate_psymtab (objfile->original_name, objfile);
          (void) psymtab;

          it = r_list_iterator (gsym_data_stream->globals_list);
          while (r_list_iter_next (it))
            {
              gdata = (SGlobal *) r_list_iter_get (it);
              sctn_header = (SIMAGE_SECTION_HEADER*) r_list_get_n (pe_stream->sections_hdrs, (gdata->segment - 1));
              if (sctn_header)
                {
                  asection *sect = section_by_name(sctn_header->name, objfile);

                  auto section = sect ? sect->index : -1;
                  auto section_address = sect ? bfd_section_vma(sect) : 0;
                  auto address = section_address + gdata->offset;
                  if (address == 0) continue; //we don't want to record unresolved symbols or something?
                  auto type = mst_unknown; //FIXME
                  reader.record_with_info(gdata->name.name, address, type, section);
                }
            }
        }

      pdb->finish_pdb_parse (pdb.get ());
    }
}
