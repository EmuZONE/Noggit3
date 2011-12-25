/*
 * ErrorHandling.h
 *
 * Description: Simple implementation of signal handlers. Include this and call RegisterErrorHandlers(); in main() once.
 *
 *  Created on: Apr 26, 2010
 *      Author: loerwald
 */

#ifndef ERRORHANDLING_H_
#define ERRORHANDLING_H_

#include <csignal>
#include <string>

#include <noggit/Log.h>

void printStacktrace();

void leave (int sig)
{
  // Reset to defaults.
  signal (SIGABRT, SIG_DFL);
  signal (SIGFPE, SIG_DFL);
  signal (SIGILL, SIG_DFL);
  signal (SIGSEGV, SIG_DFL);
  signal (SIGTERM, SIG_DFL);

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
      description =  "Termination request made to the program. (Terminate.)";
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

  exit (sig);
}

void RegisterErrorHandlers()
{
  signal (SIGABRT, leave);
  signal (SIGFPE, leave);
  signal (SIGILL, leave);
  signal (SIGSEGV, leave);
  signal (SIGTERM, leave);
}

#ifndef WIN32

#include <execinfo.h>

void printStacktrace()
{
  static const int nframes (30);

  void* array[nframes];

  const size_t size (backtrace (array, nframes));
  char** strings (backtrace_symbols (array, size));

  LogError << "Obtained " << size << " stack frames." << std::endl;

  for (size_t i (0); i < size; ++i)
    LogError << "- " << strings[i] << std::endl;

  free (strings);
}

#else

#include <windows/StackWalker.h>

void printStacktrace()
{
  StackWalker sw;
  sw.ShowCallstack();
}

#endif

#endif
