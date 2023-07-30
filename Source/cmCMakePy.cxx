#include "cmake.h"
#include "cmCMakePy.h"
#include "cmSystemTools.h"
#include "cmMakefile.h"
#include "cmExecutionStatus.h"

#include <pybind11/embed.h>
namespace py = pybind11;

static cmExecutionStatus* CurrentStatus = nullptr;
static std::exception_ptr CurrentException = nullptr;
static bool EnableDebug = false;

// NOTE:
// The shenanigans around CurrentException in this file are to make it possible
// for exceptions to travel through a chain of CMake functions/commands,
// and to only log them at the highest python entry point (if uncaught).

// sanity check
static void CheckCurrentStatus()
{
  if (!CurrentStatus) {
    const char* msg =
      "[cmakepy] INTERNAL ERROR: No current cmExecutionStatus!";
    cmSystemTools::Message(msg);
    throw std::logic_error(msg);
  }
}

// invoke a CMake function from Python
// All args are passed as if quoted by default, unless a given arg has the property "unquoted" with a true value.
static void cmakepy_invoke(const std::string& funcname, const py::args& args)
{
  if (EnableDebug)
  {
    printf("cmakepy_invoke: \"%s\"\n", funcname.c_str());
    printf("\texec.frame.module: %s\n", py::globals()["__name__"].cast<std::string>().c_str());
    for (const auto& arg : args) {
      printf("\targ: \"%s\"\n", arg.str().cast<std::string>().c_str());
    }
  }

  CheckCurrentStatus();
  long line = cmListFileContext::PythonPlaceholderLine;
  std::vector<cmListFileArgument> lfargs;
  lfargs.reserve(args.size());
  for (const auto& arg : args) {
    auto delim =
      py::hasattr(arg, "unquoted") && arg.attr("unquoted").cast<bool>()
      ? cmListFileArgument::Delimiter::Unquoted
      : cmListFileArgument::Delimiter::Quoted;
    lfargs.push_back(cmListFileArgument(arg.str().cast<std::string>(), delim, line));
  }
  cmListFileFunction func(funcname, line, line, lfargs);
  cmExecutionStatus status(CurrentStatus->GetMakefile());
  bool success = CurrentStatus->GetMakefile().ExecuteCommand(func, status);
  // First check if any exception occured downstream, regardless of return code.
  // include() for ex. won't report an error if it includes a python script,
  // as it thinks it parsed fine (because of how the cmakepy support is hacked in for now).
  if (CurrentException) {
    std::rethrow_exception(CurrentException);
  }
  if (!success || status.GetNestedError()) {
    // CMake already printed it's own call stack by now,
    // we'll print Python's at the top of the python stack (if we get there)

    // We checked for downstream exceptions above, so this here is a real first-time CMake call failure,
    // throw new exception and remember it as current
    try {
      throw std::runtime_error("cmakepy_invoke error: " + funcname + ": " + status.GetError());
    } catch (...) {
      CurrentException = std::current_exception();
      std::rethrow_exception(CurrentException);
    }
  }
}

static py::object cmakepy_get(const std::string& varname)
{
  CheckCurrentStatus();
  cmValue val = CurrentStatus->GetMakefile().GetDefinition(varname);
  return val ? py::str(*val) : py::none();
}

static void cmakepy_enable_debug(bool enable)
{
  EnableDebug = enable;
}

PYBIND11_EMBEDDED_MODULE(cmakecpp, m)
{
  m.doc() = "CMake Python frontend";
  m.def("invoke", &cmakepy_invoke, "Invoke a CMake function");
  m.def("get", &cmakepy_get, "Get a CMake variable");
  m.def("enable_debug", &cmakepy_enable_debug, "Enable CMakePy debugging (extra logging)");
  m.add_object("exported_functions", py::dict());
}

// wrap func which may call into python
template <class T>
bool PythonEntryPointWrapper(cmExecutionStatus& status, T&& func)
{
  cmExecutionStatus* PrevStatus = CurrentStatus;
  CurrentStatus = &status;
  // Clear CurrentException when entering
  // This should not be required, it should be null already, maybe put an assert here in the future instead
  CurrentException = nullptr;
  bool result = true;
  try {
    std::forward<T>(func)();
    CurrentException = nullptr; // clear on success, needed if python code handles exceptions
  } catch (const std::exception& e) {
    result = false;
    if (!CurrentException) { // no exception yet, a direct error in the python code
      status.SetError(e.what());
    } else { // error from deeper, either from python code or CMake function call
      status.SetNestedError();
    }
    CurrentException = std::current_exception();
    if (!PrevStatus) { // top of stack (no more python above us), print/report error
      CurrentException = nullptr;
      cmSystemTools::Message(std::string("Unhandled Python exception:\n") +
                             e.what());
      // status.GetMakefile().IssueMessage(MessageType::MESSAGE, e.what());
      cmSystemTools::SetFatalErrorOccurred();
    }
  }
  CurrentStatus = PrevStatus;
  return result;
}

// A Python entry point
// invoke an exported Python function from CMake
bool cmInvokePyfuncCommand(std::vector<std::string> const& args, cmExecutionStatus& status)
{
  if (args.size() < 1) {
    status.SetError("called with incorrect number of arguments");
    return false;
  }

  return PythonEntryPointWrapper(status, [&]() {
    py::list fargs;
    for (size_t i = 1; i < args.size(); ++i) {
      fargs.append(args[i]);
    }
    auto funcs = py::module_::import("cmakecpp").attr("exported_functions");
    funcs[args.at(0).c_str()].call(*fargs);
  });
}


int cmCMakePy::Instances = 0;

void cmCMakePy::InitInterpreter()
{
  //cmSystemTools::Message("[cmakepy] cmCMakePy::Interpeter initializing...");
  py::initialize_interpreter(true, 0, nullptr, false);

  // Add cmake.py dir to sys.path, so "import cmake" just works
  std::string root = cmSystemTools::GetCMakeRoot();
  auto sys = py::module_::import("sys");
  sys.attr("path").attr("insert").call(0, cmSystemTools::GetCMakeRoot() + "/Modules/CMakePy");
}

void cmCMakePy::CloseInterpreter()
{
  //cmSystemTools::Message("[cmakepy] cmCMakePy::Interpeter shutdown...");
  py::finalize_interpreter();
}

cmCMakePy::cmCMakePy(cmake* cm)
{
  if (++Instances == 1) {
    InitInterpreter();
  }
  cm->GetState()->AddBuiltinCommand("__invoke_pyfunc", cmInvokePyfuncCommand);
}

cmCMakePy::~cmCMakePy()
{
  if (--Instances == 0) {
    CloseInterpreter();
  }
}

void cmCMakePy::Run(const cmListFile& listfile, cmExecutionStatus& status)
{
  if (!listfile.Path.empty()) {
    RunFile(listfile.Path, status);
  } else {
    cmSystemTools::Error("[cmakepy] Running python from string not implemented");
  }
}

// A Python entry point
void cmCMakePy::RunFile(const std::string& filepath, cmExecutionStatus& status)
{
  PythonEntryPointWrapper(status, [&]() {
    // Run every script in scope of __main__ for now
    // TODO: research alternatives, decide what to do here...
    auto scope = py::module_::import("__main__").attr("__dict__");
    py::eval_file(filepath, scope, scope);

    // Alternative: isolate each script but inherit the current main scope
    //py::dict scope;
    //auto main = py::module_::import("__main__").attr("__dict__");
    //scope.attr("update")(main);
    //py::eval_file(filepath, scope, scope);
  });
}
