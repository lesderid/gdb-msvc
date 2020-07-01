#define HAVE_DECL_BASENAME 1
#include "demangle.h"

#include <cstring>
#include <regex>

#include <llvm/Config/llvm-config.h>
#include <llvm/Demangle/Demangle.h>

extern "C"
char* msvc_demangle(const char* sym, int options)
{
  auto mangled = sym;
  auto suffix = std::strchr(sym, '!');
  if (suffix != 0) {
    mangled = suffix + 1;
  }

  auto flags = llvm::MSDemangleFlags(llvm::MSDF_NoAccessSpecifier | llvm::MSDF_NoCallingConvention);
  if (!(options & DMGL_ANSI)) {
    /* TODO: Wait for LLVM's demangler to get a flag for this */;
  }

  //HACK: We should probably not do this on DMGL_AUTO, but the GNU C++
  //      demangler also omits the return type even without
  //      DMGL_RET_DROP...
  if ((options & (DMGL_RET_DROP | DMGL_AUTO))) {
    flags = llvm::MSDemangleFlags(flags | llvm::MSDF_NoReturnType);
  }

#if LLVM_VERSION_MAJOR > 10 || LLVM_VERSION_MAJOR == 10 && (LLVM_VERSION_MINOR > 0 || LLVM_VERSION_PATCH > 0)
  auto demangled = llvm::microsoftDemangle(mangled, nullptr, nullptr, nullptr, nullptr, flags);
#else
  auto demangled = llvm::microsoftDemangle(mangled, nullptr, nullptr, nullptr, flags);
#endif
  if (demangled == nullptr) {
    return nullptr;
  }

  auto result = std::string(demangled);

  //TODO: Use OF_NoTagSpecifier (not currently available as a MSDF_* flag)
  result = std::regex_replace(result, std::regex("(class|struct|union|enum) "), "");

  result = std::regex_replace(result, std::regex(" (\\*|&)"), "$1");

  if (!(options & DMGL_PARAMS)) {
    result = std::regex_replace(result, std::regex(" \(.*\)"), "");
  }

  return strdup(result.c_str());
}
