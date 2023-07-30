#pragma once

#include "cmListFileCache.h"

class cmake;
class cmExecutionStatus;

class cmCMakePy
{
public:
  cmCMakePy(cmake* cm);

  ~cmCMakePy();

  cmCMakePy(const cmCMakePy&) = delete;

  void Run(const cmListFile& listfile, cmExecutionStatus& status);

  void RunFile(const std::string& filepath, cmExecutionStatus& status);

private:
  static void InitInterpreter();

  static void CloseInterpreter();

  static int Instances;
};
