#define HAVE_DECL_BASENAME 1
#include "demangle.h"

#include <cstring>

#include <llvm/Demangle/Demangle.h>

extern "C"
char* msvc_demangle(const char* sym, int options)
{
  auto suffix = std::strchr(sym, '!');
  if (suffix != 0)
  {
    sym = suffix + 1;
  }

  auto flags = llvm::MSDemangleFlags(llvm::MSDF_NoAccessSpecifier | llvm::MSDF_NoCallingConvention);
  if (!(options & DMGL_ANSI)) {
    /* TODO: Wait for LLVM's demangler to get a flag for this */;
  }
  if (!(options & DMGL_PARAMS)) {
    /* TODO: Wait for LLVM's demangler to get a flag for this */;
  }

  return llvm::microsoftDemangle(sym, nullptr, nullptr, nullptr, flags);
}
