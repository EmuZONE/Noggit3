// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/Log.h>

#include <csignal>
#include <string>
#include <vector>

void printStacktrace();

void leave(int sig)
{
  // Reset to defaults.
  signal(SIGABRT, SIG_DFL);
  signal(SIGFPE, SIG_DFL);
  signal(SIGILL, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);
  signal(SIGTERM, SIG_DFL);

  std::string description;
  std::string sign;

  switch (sig)
  {
  case SIGABRT:
    sign = "SIGABRT";
    description = "Abnormal termination, such as instigated by the abort function. (Abort.)";
    break;
  case SIGFPE:
    sign = "SIGFPE";
    description = "Erroneous arithmetic operation, such as divide by 0 or overflow. (Floating point exception.)";
    break;
  case SIGILL:
    sign = "SIGILL";
    description = "An ‘invalid object program’ has been detected. This usually means that there is an illegal instruction in the program. (Illegal instruction.)";
    break;
  case SIGINT:
    sign = "SIGINT";
    description = "Interactive attention signal; on interactive systems this is usually generated by typing some ‘break-in’ key at the terminal. (Interrupt.)";
    break;
  case SIGSEGV:
    sign = "SIGSEGV";
    description = "Invalid storage access; most frequently caused by attempting to store some value in an object pointed to by a bad pointer. (Segment violation.)";
    break;
  case SIGTERM:
    sign = "SIGTERM";
    description = "Termination request made to the program. (Terminate.)";
    break;
  default:
    sign = "SIGUNK";
    description = "Unknown Exception!";
    break;
  }

  LogError << "There was an exception of type \""
    << sign
    << "\"\n\""
    << description
    << "\".\nPlease excuse the inconvenience. You may want to report this error including the log to the developers.\n"
    << std::endl;

  printStacktrace();

  exit(sig);
}

void RegisterErrorHandlers()
{
  signal(SIGABRT, leave);
  signal(SIGFPE, leave);
  signal(SIGILL, leave);
  signal(SIGSEGV, leave);
  signal(SIGTERM, leave);
}

#ifndef WIN32

#include <execinfo.h>

void printStacktrace()
{
  std::vector<void*> frames (32);

  std::size_t actual (0);
  while ((actual = backtrace (frames.data(), frames.size())) == frames.size())
  {
    frames.resize (frames.size() * 2);
  }
  frames.resize (actual);
  char** strings(backtrace_symbols(frames.data(), frames.size()));

  LogError << "Obtained " << frames.size() << " stack frames." << std::endl;

  for (size_t i(0); i < frames.size(); ++i)
    LogError << "- " << strings[i] << std::endl;

  free(strings);
}

#else

#include <win/StackWalker.h>

void printStacktrace()
{
  StackWalker sw;
  sw.ShowCallstack();
}

#endif
