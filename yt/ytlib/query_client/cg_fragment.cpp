#include "stdafx.h"
#include "cg_fragment.h"
#include "cg_routine_registry.h"

#include "private.h"

#include <core/misc/lazy_ptr.h>

#include <core/concurrency/action_queue.h>

#include <llvm/ADT/Triple.h>

#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>

#include <llvm/PassManager.h>

#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Host.h>

namespace NYT {
namespace NQueryClient {

static auto& Logger = QueryClientLogger;

// XXX(sandello): Due to http://llvm.org/bugs/show_bug.cgi?id=15750
// we have to serialize all MCJIT operations through a single thread.
static TLazyIntrusivePtr<NConcurrency::TActionQueue> McjitThread(
    NConcurrency::TActionQueue::CreateFactory("Mcjit"));

////////////////////////////////////////////////////////////////////////////////

class TCGMemoryManager
    : public llvm::SectionMemoryManager
{
public:
    TCGMemoryManager()
    { }

    ~TCGMemoryManager()
    { }

    virtual uint64_t getSymbolAddress(const std::string& name) override
    {
        uint64_t address = 0;

        address = llvm::SectionMemoryManager::getSymbolAddress(name);
        if (address) {
            return address;
        }

        address = TRoutineRegistry::Get()->GetAddress(name.c_str());
        if (address) {
            return address;
        }

        return 0;
    }
};

class TCGFragment::TImpl
{
public:
    TImpl()
        : MainFunction_(nullptr)
        , CompiledMainFunction_(nullptr)
    {
        Context_.setDiagnosticHandler(&TImpl::DiagnosticHandler, nullptr);

        // Infer host parameters.
        auto hostCpu = llvm::sys::getHostCPUName();
        auto hostTriple = llvm::Triple::normalize(
            llvm::sys::getProcessTriple()
#ifdef _win_
            + "-elf"
#endif
        );

        // Create module.
        auto module = std::make_unique<llvm::Module>("cgfragment", Context_);
        module->setTargetTriple(hostTriple);

        // Create engine.
        std::string what;
        Engine_.reset(llvm::EngineBuilder(module.get())
            .setEngineKind(llvm::EngineKind::JIT)
            .setOptLevel(llvm::CodeGenOpt::Default)
            .setUseMCJIT(true)
            .setMCJITMemoryManager(new TCGMemoryManager())
            .setMCPU(hostCpu)
            .setErrorStr(&what)
            .create());

        if (!Engine_) {
            THROW_ERROR_EXCEPTION("Could not create llvm::ExecutionEngine: %s", what.c_str());
        }

        Module_ = module.release();
        Module_->setDataLayout(Engine_->getDataLayout()->getStringRepresentation());

        LOG_DEBUG("Created a new codegenerated fragment (HostCpu: %s, HostTriple: %s)",
            hostCpu.str().c_str(),
            hostTriple.c_str());
    }

    llvm::LLVMContext& GetContext()
    {
        return Context_;
    }

    llvm::Module* GetModule()
    {
        return Module_;
    }

    llvm::Function* GetRoutine(const Stroka& symbol)
    {
        auto type = TRoutineRegistry::Get()->GetTypeBuilder(symbol)(Context_);

        auto it = CachedRoutines_.find(symbol);
        if (it == CachedRoutines_.end()) {
            auto routine = llvm::Function::Create(
                type,
                llvm::Function::ExternalLinkage,
                symbol.c_str(),
                Module_);

            it = CachedRoutines_.emplace(symbol, routine).first;
        }

        YCHECK(it->second->getFunctionType() == type);
        return it->second;
    }

    void SetMainFunction(llvm::Function* mainFunction)
    {
        auto parent = Module_;
        auto type = llvm::TypeBuilder<TCodegenedFunction, false>::get(Context_);
        YCHECK(mainFunction->getParent() == parent);
        YCHECK(mainFunction->getType() == type);
        YCHECK(!MainFunction_);
        MainFunction_ = mainFunction;
    }

    TCodegenedFunction GetCompiledMainFunction()
    {
        if (!CompiledMainFunction_) {
            BIND(&TCGFragment::TImpl::Compile, this)
                .AsyncVia(McjitThread->GetInvoker())
                .Run()
                .Get();
        }
        YCHECK(CompiledMainFunction_);
        return CompiledMainFunction_;
    }

    bool IsCompiled()
    {
        return CompiledMainFunction_;
    }

private:
    void Compile()
    {
        YCHECK(MainFunction_);
        YCHECK(!CompiledMainFunction_);

        YCHECK(!llvm::verifyModule(*Module_, &llvm::errs()));

        llvm::PassManagerBuilder passManagerBuilder;
        passManagerBuilder.OptLevel = 1;
        passManagerBuilder.SizeLevel = 0;
        passManagerBuilder.Inliner = llvm::createFunctionInliningPass();

        std::unique_ptr<llvm::FunctionPassManager> functionPassManager_;
        std::unique_ptr<llvm::PassManager> modulePassManager_;

        functionPassManager_ = std::make_unique<llvm::FunctionPassManager>(Module_);
        functionPassManager_->add(new llvm::DataLayoutPass(Module_));
        passManagerBuilder.populateFunctionPassManager(*functionPassManager_);

        functionPassManager_->doInitialization();
        for (auto it = Module_->begin(), jt = Module_->end(); it != jt; ++it) {
            if (!it->isDeclaration()) {
                functionPassManager_->run(*it);
            }
        }
        functionPassManager_->doFinalization();

        modulePassManager_ = std::make_unique<llvm::PassManager>();
        modulePassManager_->add(new llvm::DataLayoutPass(Module_));
        passManagerBuilder.populateModulePassManager(*modulePassManager_);

        modulePassManager_->run(*Module_);

        //Module_->dump();
        Engine_->finalizeObject();

        CompiledMainFunction_ = reinterpret_cast<TCodegenedFunction>(
            Engine_->getPointerToFunction(MainFunction_));
    }

    static void DiagnosticHandler(const llvm::DiagnosticInfo& info, void* /*opaque*/)
    {
        std::string what;
        llvm::raw_string_ostream os(what);
        llvm::DiagnosticPrinterRawOStream printer(os);

        info.print(printer);

        LOG_INFO("LLVM has triggered a message: %s/%s: %s",
            DiagnosticSeverityToString(info.getSeverity()),
            DiagnosticKindToString((llvm::DiagnosticKind)info.getKind()),
            what.c_str());

        llvm::errs() << "!!! LLVM: " << what << "\n";
    }

    static const char* DiagnosticKindToString(llvm::DiagnosticKind kind)
    {
        switch (kind) {
            case llvm::DK_InlineAsm:
                return "DK_InlineAsm";
            case llvm::DK_StackSize:
                return "DK_StackSize";
            case llvm::DK_DebugMetadataVersion:
                return "DK_DebugMetadataVersion";
            case llvm::DK_FirstPluginKind:
                return "DK_FirstPluginKind";
            default:
                return "DK_(?)";
        }
        YUNREACHABLE();
    }

    static const char* DiagnosticSeverityToString(llvm::DiagnosticSeverity severity)
    {
        switch (severity) {
            case llvm::DS_Error:
                return "DS_Error";
            case llvm::DS_Warning:
                return "DS_Warning";
            case llvm::DS_Note:
                return "DS_Note";
            default:
                return "DS_(?)";
        }
        YUNREACHABLE();
    }

private:
    llvm::LLVMContext Context_;
    llvm::Module* Module_;

    std::unique_ptr<llvm::ExecutionEngine> Engine_;

    llvm::Function* MainFunction_;
    TCodegenedFunction CompiledMainFunction_;

    std::unordered_map<Stroka, llvm::Function*> CachedRoutines_;

};

TCGFragment::TCGFragment()
    : Impl_(std::make_unique<TImpl>())
{ }

TCGFragment::~TCGFragment()
{ }

llvm::LLVMContext& TCGFragment::GetContext()
{
    return Impl_->GetContext();
}

llvm::Module* TCGFragment::GetModule()
{
    return Impl_->GetModule();
}

llvm::Function* TCGFragment::GetRoutine(const Stroka& symbol)
{
    return Impl_->GetRoutine(symbol);
}

void TCGFragment::SetMainFunction(llvm::Function* mainFunction)
{
    Impl_->SetMainFunction(mainFunction);
}

TCodegenedFunction TCGFragment::GetCompiledMainFunction()
{
    return Impl_->GetCompiledMainFunction();
}

bool TCGFragment::IsCompiled()
{
    return Impl_->IsCompiled();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

