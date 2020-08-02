/* PDB support for BFD. */

#include "pdb.h"

using namespace llvm::codeview;

const llvm::object::coff_section &
bfd_pdb_get_llvm_section_header (asection *section)
{
  //TODO: check if this bfd is a PDB bfd
  return *(llvm::object::coff_section *) section->used_by_bfd;
}

llvm::pdb::PDBFile &
bfd_pdb_get_llvm_pdb_file (bfd *abfd)
{
  //TODO: check if this bfd is a PDB bfd
  return *abfd->tdata.pdb_data->llvmPdbFile;
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

    return fileSize = stat.st_size;
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
      printf ("%s: error: %s\n", abfd->filename, toString (std::move (ec)).c_str ());
      return nullptr;
    }

  auto resultBuffer = bfd_alloc (abfd, sizeof (bfd_pdb_data_struct));
  auto result = new (resultBuffer) bfd_pdb_data_struct;
  result->llvmPdbFile = std::move (pdbFile);
  return result;
}

bool
bfd_pdb_get_sections (bfd *abfd)
{
  auto & pdbFile = bfd_pdb_get_llvm_pdb_file(abfd);

  auto dbi = pdbFile.getPDBDbiStream ();
  auto streamIndex = dbi->getDebugStreamIndex (llvm::pdb::DbgHeaderType::SectionHdr);
  auto stream = pdbFile.createIndexedStream (streamIndex);

  llvm::ArrayRef<llvm::object::coff_section> headers;
  auto headerCount = stream->getLength () / sizeof (llvm::object::coff_section);
  llvm::BinaryStreamReader reader (*stream);
  if (reader.readArray (headers, headerCount))
    {
      return false;
    }

  for (auto & header: headers)
    {
      auto headerCopyBuffer = bfd_alloc (abfd,
                                         sizeof (llvm::object::coff_section));
      auto headerCopy = new (headerCopyBuffer) llvm::object::coff_section (header);

      asection *section = bfd_make_section_with_flags (abfd,
                                                       headerCopy->Name,
                                                       SEC_LOAD);
      section->vma = headerCopy->VirtualAddress;
      section->size = headerCopy->VirtualSize;
      section->used_by_bfd = headerCopy;
    }

  return true;
}

const bfd_target *
bfd_pdb_check_format (bfd *abfd)
{
  if ((abfd->tdata.pdb_data = get_bfd_pdb_data (abfd)))
    {
      if (!bfd_pdb_get_sections (abfd))
        goto fail;

      return abfd->xvec;
    }

  fail:
  bfd_set_error (bfd_error_wrong_format);
  return nullptr;
}

bfd_boolean
bfd_pdb_close_and_cleanup (bfd *abfd)
{
  return TRUE;
}

bfd_boolean
bfd_pdb_bfd_free_cached_info (bfd *abfd)
{
  return FALSE;
}

#define bfd_pdb_new_section_hook _bfd_generic_new_section_hook
#define bfd_pdb_get_section_contents _bfd_generic_get_section_contents
#define bfd_pdb_get_section_contents_in_window _bfd_generic_get_section_contents_in_window

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
    BFD_JUMP_TABLE_SYMBOLS (_bfd_nosymbols),
    BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
    BFD_JUMP_TABLE_WRITE (_bfd_nowrite),
    BFD_JUMP_TABLE_LINK (_bfd_nolink),
    BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

    nullptr,

    nullptr
  };
