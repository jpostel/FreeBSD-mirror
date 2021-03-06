//===--- Tools.h - Tool Implementations -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_DRIVER_TOOLS_H_
#define CLANG_LIB_DRIVER_TOOLS_H_

#include "clang/Driver/Tool.h"
#include "clang/Driver/Types.h"
#include "clang/Driver/Util.h"

#include "llvm/Support/Compiler.h"

namespace clang {
namespace driver {
  class Driver;

namespace toolchains {
  class Darwin;
}

namespace tools {

  /// \brief Clang compiler tool.
  class LLVM_LIBRARY_VISIBILITY Clang : public Tool {
    void AddPreprocessingOptions(const Driver &D,
                                 const ArgList &Args,
                                 ArgStringList &CmdArgs,
                                 const InputInfo &Output,
                                 const InputInfoList &Inputs) const;

    void AddARMTargetArgs(const ArgList &Args, ArgStringList &CmdArgs) const;
    void AddMIPSTargetArgs(const ArgList &Args, ArgStringList &CmdArgs) const;
    void AddX86TargetArgs(const ArgList &Args, ArgStringList &CmdArgs) const;

  public:
    Clang(const ToolChain &TC) : Tool("clang", "clang frontend", TC) {}

    virtual bool hasGoodDiagnostics() const { return true; }
    virtual bool hasIntegratedAssembler() const { return true; }
    virtual bool hasIntegratedCPP() const { return true; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };

  /// \brief Clang integrated assembler tool.
  class LLVM_LIBRARY_VISIBILITY ClangAs : public Tool {
  public:
    ClangAs(const ToolChain &TC) : Tool("clang::as",
                                        "clang integrated assembler", TC) {}

    virtual bool hasGoodDiagnostics() const { return true; }
    virtual bool hasIntegratedAssembler() const { return false; }
    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };

  /// gcc - Generic GCC tool implementations.
namespace gcc {
  class LLVM_LIBRARY_VISIBILITY Common : public Tool {
  public:
    Common(const char *Name, const char *ShortName,
           const ToolChain &TC) : Tool(Name, ShortName, TC) {}

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;

    /// RenderExtraToolArgs - Render any arguments necessary to force
    /// the particular tool mode.
    virtual void RenderExtraToolArgs(const JobAction &JA,
                                     ArgStringList &CmdArgs) const = 0;
  };


  class LLVM_LIBRARY_VISIBILITY Preprocess : public Common {
  public:
    Preprocess(const ToolChain &TC) : Common("gcc::Preprocess",
                                             "gcc preprocessor", TC) {}

    virtual bool hasGoodDiagnostics() const { return true; }
    virtual bool hasIntegratedCPP() const { return false; }

    virtual void RenderExtraToolArgs(const JobAction &JA,
                                     ArgStringList &CmdArgs) const;
  };

  class LLVM_LIBRARY_VISIBILITY Precompile : public Common  {
  public:
    Precompile(const ToolChain &TC) : Common("gcc::Precompile",
                                             "gcc precompile", TC) {}

    virtual bool hasGoodDiagnostics() const { return true; }
    virtual bool hasIntegratedCPP() const { return true; }

    virtual void RenderExtraToolArgs(const JobAction &JA,
                                     ArgStringList &CmdArgs) const;
  };

  class LLVM_LIBRARY_VISIBILITY Compile : public Common  {
  public:
    Compile(const ToolChain &TC) : Common("gcc::Compile",
                                          "gcc frontend", TC) {}

    virtual bool hasGoodDiagnostics() const { return true; }
    virtual bool hasIntegratedCPP() const { return true; }

    virtual void RenderExtraToolArgs(const JobAction &JA,
                                     ArgStringList &CmdArgs) const;
  };

  class LLVM_LIBRARY_VISIBILITY Assemble : public Common  {
  public:
    Assemble(const ToolChain &TC) : Common("gcc::Assemble",
                                           "assembler (via gcc)", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void RenderExtraToolArgs(const JobAction &JA,
                                     ArgStringList &CmdArgs) const;
  };

  class LLVM_LIBRARY_VISIBILITY Link : public Common  {
  public:
    Link(const ToolChain &TC) : Common("gcc::Link",
                                       "linker (via gcc)", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void RenderExtraToolArgs(const JobAction &JA,
                                     ArgStringList &CmdArgs) const;
  };
} // end namespace gcc

namespace darwin {
  class LLVM_LIBRARY_VISIBILITY DarwinTool : public Tool {
  protected:
    void AddDarwinArch(const ArgList &Args, ArgStringList &CmdArgs) const;

    const toolchains::Darwin &getDarwinToolChain() const {
      return reinterpret_cast<const toolchains::Darwin&>(getToolChain());
    }

  public:
    DarwinTool(const char *Name, const char *ShortName,
               const ToolChain &TC) : Tool(Name, ShortName, TC) {}
  };

  class LLVM_LIBRARY_VISIBILITY CC1 : public DarwinTool  {
  public:
    static const char *getBaseInputName(const ArgList &Args,
                                 const InputInfoList &Input);
    static const char *getBaseInputStem(const ArgList &Args,
                                 const InputInfoList &Input);
    static const char *getDependencyFileName(const ArgList &Args,
                                             const InputInfoList &Inputs);

  protected:
    const char *getCC1Name(types::ID Type) const;

    void AddCC1Args(const ArgList &Args, ArgStringList &CmdArgs) const;
    void AddCC1OptionsArgs(const ArgList &Args, ArgStringList &CmdArgs,
                           const InputInfoList &Inputs,
                           const ArgStringList &OutputArgs) const;
    void AddCPPOptionsArgs(const ArgList &Args, ArgStringList &CmdArgs,
                           const InputInfoList &Inputs,
                           const ArgStringList &OutputArgs) const;
    void AddCPPUniqueOptionsArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs,
                                 const InputInfoList &Inputs) const;
    void AddCPPArgs(const ArgList &Args, ArgStringList &CmdArgs) const;

  public:
    CC1(const char *Name, const char *ShortName,
        const ToolChain &TC) : DarwinTool(Name, ShortName, TC) {}

    virtual bool hasGoodDiagnostics() const { return true; }
    virtual bool hasIntegratedCPP() const { return true; }
  };

  class LLVM_LIBRARY_VISIBILITY Preprocess : public CC1  {
  public:
    Preprocess(const ToolChain &TC) : CC1("darwin::Preprocess",
                                          "gcc preprocessor", TC) {}

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };

  class LLVM_LIBRARY_VISIBILITY Compile : public CC1  {
  public:
    Compile(const ToolChain &TC) : CC1("darwin::Compile", "gcc frontend", TC) {}

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };

  class LLVM_LIBRARY_VISIBILITY Assemble : public DarwinTool  {
  public:
    Assemble(const ToolChain &TC) : DarwinTool("darwin::Assemble",
                                               "assembler", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };

  class LLVM_LIBRARY_VISIBILITY Link : public DarwinTool  {
    void AddLinkArgs(const ArgList &Args, ArgStringList &CmdArgs) const;

  public:
    Link(const ToolChain &TC) : DarwinTool("darwin::Link", "linker", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };

  class LLVM_LIBRARY_VISIBILITY Lipo : public DarwinTool  {
  public:
    Lipo(const ToolChain &TC) : DarwinTool("darwin::Lipo", "lipo", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };

  class LLVM_LIBRARY_VISIBILITY Dsymutil : public DarwinTool  {
  public:
    Dsymutil(const ToolChain &TC) : DarwinTool("darwin::Dsymutil",
                                               "dsymutil", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
}

  /// openbsd -- Directly call GNU Binutils assembler and linker
namespace openbsd {
  class LLVM_LIBRARY_VISIBILITY Assemble : public Tool  {
  public:
    Assemble(const ToolChain &TC) : Tool("openbsd::Assemble", "assembler",
                                         TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
  class LLVM_LIBRARY_VISIBILITY Link : public Tool  {
  public:
    Link(const ToolChain &TC) : Tool("openbsd::Link", "linker", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
} // end namespace openbsd

  /// freebsd -- Directly call GNU Binutils assembler and linker
namespace freebsd {
  class LLVM_LIBRARY_VISIBILITY Assemble : public Tool  {
  public:
    Assemble(const ToolChain &TC) : Tool("freebsd::Assemble", "assembler",
                                         TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
  class LLVM_LIBRARY_VISIBILITY Link : public Tool  {
  public:
    Link(const ToolChain &TC) : Tool("freebsd::Link", "linker", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
} // end namespace freebsd

  /// linux -- Directly call GNU Binutils assembler and linker
namespace linuxtools {
  class LLVM_LIBRARY_VISIBILITY Assemble : public Tool  {
  public:
    Assemble(const ToolChain &TC) : Tool("linux::Assemble", "assembler",
                                         TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
}
  /// minix -- Directly call GNU Binutils assembler and linker
namespace minix {
  class LLVM_LIBRARY_VISIBILITY Assemble : public Tool  {
  public:
    Assemble(const ToolChain &TC) : Tool("minix::Assemble", "assembler",
                                         TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
  class LLVM_LIBRARY_VISIBILITY Link : public Tool  {
  public:
    Link(const ToolChain &TC) : Tool("minix::Link", "linker", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
} // end namespace minix

  /// auroraux -- Directly call GNU Binutils assembler and linker
namespace auroraux {
  class LLVM_LIBRARY_VISIBILITY Assemble : public Tool  {
  public:
    Assemble(const ToolChain &TC) : Tool("auroraux::Assemble", "assembler",
                                         TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
  class LLVM_LIBRARY_VISIBILITY Link : public Tool  {
  public:
    Link(const ToolChain &TC) : Tool("auroraux::Link", "linker", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
} // end namespace auroraux

  /// dragonfly -- Directly call GNU Binutils assembler and linker
namespace dragonfly {
  class LLVM_LIBRARY_VISIBILITY Assemble : public Tool  {
  public:
    Assemble(const ToolChain &TC) : Tool("dragonfly::Assemble", "assembler",
                                         TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
  class LLVM_LIBRARY_VISIBILITY Link : public Tool  {
  public:
    Link(const ToolChain &TC) : Tool("dragonfly::Link", "linker", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
} // end namespace dragonfly

  /// Visual studio tools.
namespace visualstudio {
  class LLVM_LIBRARY_VISIBILITY Link : public Tool  {
  public:
    Link(const ToolChain &TC) : Tool("visualstudio::Link", "linker", TC) {}

    virtual bool hasIntegratedCPP() const { return false; }

    virtual void ConstructJob(Compilation &C, const JobAction &JA,
                              const InputInfo &Output,
                              const InputInfoList &Inputs,
                              const ArgList &TCArgs,
                              const char *LinkingOutput) const;
  };
} // end namespace visualstudio

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // CLANG_LIB_DRIVER_TOOLS_H_
