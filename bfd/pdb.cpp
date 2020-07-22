/* PDB support for BFD. */

#include "pdb.h"

/* Called when the BFD is being closed to do any necessary cleanup.  */
bfd_boolean
bfd_pdb_close_and_cleanup (bfd *abfd)
{
  return TRUE;
}

/* Ask the BFD to free all cached information.  */
bfd_boolean
bfd_pdb_bfd_free_cached_info (bfd *abfd)
{
  return FALSE;
}

/* Called when a new section is created.  */
#define bfd_pdb_new_section_hook _bfd_generic_new_section_hook

/* Read the contents of a section.  */
#define bfd_pdb_get_section_contents _bfd_generic_get_section_contents
#define bfd_pdb_get_section_contents_in_window _bfd_generic_get_section_contents_in_window

long
bfd_pdb_get_symtab_upper_bound (bfd *abfd)
{
  return -1;
}

long
bfd_pdb_canonicalize_symtab (bfd *abfd, asymbol **alocation)
{
  return -1;
}

#define bfd_pdb_make_empty_symbol _bfd_generic_make_empty_symbol

void
bfd_pdb_print_symbol (bfd *abfd,
                      void *afile,
                      asymbol *symbol,
                      bfd_print_symbol_type how)
{
}

void
bfd_pdb_get_symbol_info (bfd *abfd,
                         asymbol *symbol,
                         symbol_info *ret)
{
}

#define bfd_pdb_get_symbol_version_string _bfd_nosymbols_get_symbol_version_string
#define bfd_pdb_bfd_is_local_label_name  bfd_generic_is_local_label_name
#define bfd_pdb_bfd_is_target_special_symbol _bfd_bool_bfd_asymbol_false
#define bfd_pdb_get_lineno _bfd_nosymbols_get_lineno

bfd_boolean
bfd_pdb_find_nearest_line (bfd *abfd,
                           asymbol **symbols,
                           asection *section,
                           bfd_vma offset,
                           const char **filename_ptr,
                           const char **functionname_ptr,
                           unsigned int *line_pt,
                           unsigned int *discriminator_ptr)
{
  return FALSE;
}

#define bfd_pdb_find_line _bfd_nosymbols_find_line
#define bfd_pdb_find_inliner_info _bfd_nosymbols_find_inliner_info

/* Back-door to allow format-aware applications to create debug symbols
   while using BFD for everything else.  Currently used by the assembler
   when creating COFF files.  */
#define bfd_pdb_bfd_make_debug_symbol _bfd_nosymbols_bfd_make_debug_symbol

#define bfd_pdb_read_minisymbols _bfd_nosymbols_read_minisymbols
#define bfd_pdb_minisymbol_to_symbol _bfd_nosymbols_minisymbol_to_symbol

static bfd_pdb_data_struct *
get_bfd_pdb_data (bfd *abfd)
{
  return NULL;
}

void
bfd_pdb_get_sections (bfd *abfd)
{
}

const bfd_target *
bfd_pdb_check_format (bfd *abfd)
{
  if ((abfd->tdata.pdb_data = get_bfd_pdb_data (abfd)))
    {
      if (true)
        {
          goto fail;
        }

      return abfd->xvec;
    }

  fail:
  bfd_set_error (bfd_error_wrong_format);
  return NULL;
}

extern "C"
const bfd_target pdb_vec =
  {
    "pdb",      /* Name.  */
    bfd_target_pdb_flavour,  /* Flavour.  */
    BFD_ENDIAN_LITTLE,    /* Byteorder.  */
    BFD_ENDIAN_LITTLE,    /* Header_byteorder.  */

    /* FIXME: These might not be correct */
    (HAS_RELOC | EXEC_P |    /* Object flags.  */
     HAS_LINENO | HAS_DEBUG |
     HAS_SYMS | HAS_LOCALS | DYNAMIC | WP_TEXT | D_PAGED),
    (SEC_ALLOC | SEC_LOAD | SEC_READONLY | SEC_CODE | SEC_DATA
     | SEC_DEBUGGING
     | SEC_ROM | SEC_HAS_CONTENTS), /* Section_flags.  */
    0,        /* Symbol_leading_char.  */
    '/',        /* AR_pad_char.  */
    15,        /* AR_max_namelen.  */
    0,        /* match priority.  */

    bfd_getl64, bfd_getl_signed_64, bfd_putb64,
    bfd_getl32, bfd_getl_signed_32, bfd_putb32,
    bfd_getl16, bfd_getl_signed_16, bfd_putb16,  /* Data.  */
    bfd_getl64, bfd_getl_signed_64, bfd_putb64,
    bfd_getl32, bfd_getl_signed_32, bfd_putb32,
    bfd_getl16, bfd_getl_signed_16, bfd_putb16,  /* Headers.  */

    {        /* bfd_check_format.  */
      _bfd_dummy_target,
      bfd_pdb_check_format,    /* bfd_check_format.  */
      _bfd_dummy_target,
      _bfd_dummy_target,
    },
    {        /* bfd_set_format.  */
      _bfd_bool_bfd_false_error,
      _bfd_bool_bfd_false,
      _bfd_bool_bfd_false_error,
      _bfd_bool_bfd_false_error,
    },
    {        /* bfd_write_contents.  */
      _bfd_bool_bfd_false_error,
      _bfd_bool_bfd_false,
      _bfd_bool_bfd_false_error,
      _bfd_bool_bfd_false_error,
    },

    BFD_JUMP_TABLE_GENERIC (bfd_pdb),
    BFD_JUMP_TABLE_COPY (_bfd_generic),
    BFD_JUMP_TABLE_CORE (_bfd_nocore),
    BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive),
    BFD_JUMP_TABLE_SYMBOLS (bfd_pdb),
    BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
    BFD_JUMP_TABLE_WRITE (_bfd_nowrite),
    BFD_JUMP_TABLE_LINK (_bfd_nolink),
    BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

    NULL,

    NULL
  };
