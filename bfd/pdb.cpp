/* PDB support for BFD. */

#include "pdb.h"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedLocalVariable"
#pragma ide diagnostic ignored "UnusedMacroInspection"
#pragma ide diagnostic ignored "UnusedGlobalDeclarationInspection"

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

#pragma clang diagnostic pop

class BfdByteStream;

static bfd_pdb_data_struct *
get_bfd_pdb_data (bfd *abfd)
{
  //TODO: check file magic

  auto allocator = std::make_unique<llvm::BumpPtrAllocator> ();

  auto stream = std::make_unique<BfdByteStream> (abfd);
  auto pdbFile = std::make_unique<llvm::pdb::PDBFile> (abfd->filename, std::move (stream), *allocator);

  //TODO: proper error handling, only if magic says we're actually reading a PDB file
  auto ec = pdbFile->parseFileHeaders ();
  if (ec)
    {
      //printf ("%s: error: %s\n", abfd->filename, toString (std::move (ec)).c_str ());
      return nullptr;
    }

  ec = pdbFile->parseStreamData ();
  if (ec)
    {
      //printf ("%s: error: %s\n", abfd->filename, toString (std::move (ec)).c_str ());
      return nullptr;
    }

  auto session = std::make_unique<llvm::pdb::NativeSession> (std::move (pdbFile),
                                                             std::move (allocator));

  auto resultBuffer = bfd_alloc (abfd, sizeof (bfd_pdb_data_struct));
  auto result = new (resultBuffer) bfd_pdb_data_struct;
  result->session = std::move (session);
  return result;
}

bool
bfd_pdb_get_sections (bfd *abfd)
{
  auto & session = abfd->tdata.pdb_data->session;
  auto & pdbFile = session->getPDBFile ();

  auto dbi = pdbFile.getPDBDbiStream ();
  auto streamIndex = dbi->getDebugStreamIndex (llvm::pdb::DbgHeaderType::SectionHdr);
  auto stream = pdbFile.createIndexedStream (streamIndex);

  llvm::ArrayRef<llvm::object::coff_section> headers;
  auto headerCount = stream->getLength () / sizeof (llvm::object::coff_section);
  llvm::BinaryStreamReader reader (*stream);
  if(reader.readArray (headers, headerCount))
    {
      return false;
    }

  for (auto & header: headers)
    {
      asection *section = bfd_make_section_with_flags (abfd,
                                                       header.Name,
                                                       SEC_LOAD);
      section->vma = header.VirtualAddress;
      section->size = header.VirtualSize;
      //section->userdata = header;
    }

    return true;
}

class BfdByteStream : public llvm::BinaryStream {
 public:
  explicit BfdByteStream (bfd *abfd) : abfd (abfd)
  {
  }

  llvm::support::endianness getEndian () const override
  {
    return llvm::support::little;
  }

  llvm::Error readBytes (uint32_t Offset, uint32_t Size, llvm::ArrayRef<uint8_t> & Buffer) override
  {
    //We need to cache the whole PDB file in memory:
    //During parsing, the LLVM functions first read one block of PDB data into an ArrayRef. They
    //then just assume all blocks are stored contiguously in memory and simply change the `size`
    //field of the ArrayRef instead of actually reading the remaining blocks...

    if (!cached)
      {
        auto ec = this->createCache ();
        if (ec) return ec;
      }

    //printf ("%s: readBytes(offset=%d,size=%d)\n", abfd->filename, Offset, Size);

    Buffer = llvm::ArrayRef<uint8_t> (cache + Offset, (size_t) Size);
    return llvm::Error::success ();
  }

  llvm::Error
  readLongestContiguousChunk (uint32_t Offset, llvm::ArrayRef<uint8_t> & Buffer) override
  {
    return readBytes (Offset, getLength () - Offset, Buffer);
  }

  uint32_t getLength () override
  {
    if (fileSize != -1)
      return fileSize;

    struct stat stat{};
    if (bfd_stat (abfd, &stat) < 0)
      return -1;

    fileSize = stat.st_size;

    //printf("stat.st_size=%ld\n", stat.st_size);
    return fileSize;
  }

 private:
  llvm::Error
  createCache ()
  {
    if (cached) return llvm::Error::success ();

    auto length = getLength ();

    cache = static_cast<uint8_t *>(bfd_alloc (abfd, length));
    if (bfd_seek (abfd, 0, SEEK_SET) != 0)
      return llvm::createStringError (std::error_code (), "EOF");
    if (bfd_bread (cache, length, abfd) != length)
      return llvm::createStringError (std::error_code (), "EOF");

    cached = true;

    return llvm::Error::success ();
  }

  bfd *abfd;
  bool cached = false;
  uint8_t *cache = nullptr;
  size_t fileSize = -1;
};

const bfd_target *
bfd_pdb_check_format (bfd *abfd)
{
  if ((abfd->tdata.pdb_data = get_bfd_pdb_data (abfd)))
    {
      auto & pdbData = abfd->tdata.pdb_data;

      if (!bfd_pdb_get_sections (abfd))
        goto fail;

      return abfd->xvec;
    }

  fail:
  bfd_set_error (bfd_error_wrong_format);
  return nullptr;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"

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

    nullptr,

    nullptr
  };

#pragma clang diagnostic pop