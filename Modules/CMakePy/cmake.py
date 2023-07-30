import cmakecpp


class CMakeFunctionsWrapper:

    def __getattr__(self, attrname):
        return self.wrap_invoke(attrname);

    def wrap_invoke(self, func):
        def wrapped(*args):
            cmakecpp.invoke(func, *args)
        return wrapped


class Unquoted:

    def __init__(self, s):
        self.s = s
        self.unquoted = True

    def __str__(self):
        return str(self.s)


fn = CMakeFunctionsWrapper()  # ex.: fn.cmfunc("arg1", "arg2") == invoke("cmfunc", "arg1", "arg2")
uq = Unquoted  # alias
invoke = cmakecpp.invoke  # func: invoke("cmake_func", "arg1", "arg2", ...)
get = cmakecpp.get  # func: get("cmake_var_name"), ret: None if unset
enable_debug = cmakecpp.enable_debug  # func: enable_debug(True/False)
exported_functions = cmakecpp.exported_functions  # dict: ["name"] = func


def export_function(func, name=None):
    if not name: name = func.__name__
    from inspect import signature, Parameter
    sig = signature(func)
    i = 0;
    argnames = [] # arg0, arg1, ...
    argrefs = []  # ${arg0}, ${arg1}, ...
    for p in sig.parameters.values():
        if p.kind == Parameter.POSITIONAL_OR_KEYWORD:
            argnames.append(f"arg{i}")
            argrefs.append(f"${{arg{i}}}")
            i += 1
        #if p.kind == Parameter.VAR_POSITIONAL:
        #    argrefs.append(uq("${ARGN}")) # unquoted
    argrefs.append(uq("${ARGN}")) # unquoted
    # NOTE: We always pass ARGN, meaning python will throw if more args are given
    #       from CMake but there's no *args in the python function signature.
    #       To silently ignore extra args instead, comment the line above
    #       and uncomment the 2 line "if ... VAR_POSITIONAL" section above that.
    # TODO: Decide which approach is better from the two.
    fn.function(name, *argnames)        # function(name arg1 arg2 ...)
    fn.__invoke_pyfunc(name, *argrefs)  # __invoke_pyfunc(name "${arg1}" "${arg2}" ... ${ARGN})
    fn.endfunction()
    cmakecpp.exported_functions[name] = func


def get_list(varname):
    var = get(varname)
    return var.split(';') if var is not None else None


#####################################
### CMake rich Python API follows ###
#####################################

# NOTE: Just a few dummies for now...


def set(varname, *args):
    fn.set(varname, *args)


def cmake_minimum_required(version):
    fn.cmake_minimum_required("VERSION", version, "FATAL_ERROR")


def project(name, version=None, description=None, homepage_url=None, languages=[]):
    args=[name]
    if version is not None: args += ["VERSION", version]
    if description is not None: args += ["DESCRIPTION", description]
    if homepage_url is not None: args += ["HOMEPAGE_URL", homepage_url]
    if languages: args += ["LANGUAGES", *languages]
    fn.project(*args)


def add_executable(name, files):
    fn.add_executable(name, *files)
