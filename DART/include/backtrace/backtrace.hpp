#pragma once

#include <iostream>
#include <execinfo.h>
#include <signal.h>

#include "log/log.hpp"


inline void PrintStackTrace() {
  const int max_frames = 64;
  void *addrlist[max_frames + 1];

  // retrieve current stack addresses
  int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void *));

  if (addrlen == 0) {
    printf("  \n");
    return;
  }

  // create readable strings to each frame.
  char **symbollist = backtrace_symbols(addrlist, addrlen);

  // print the stack trace.
  for (int i = 4; i < addrlen; i++) {
    printf("%s\n", symbollist[i]);
  }

  free(symbollist);
}

inline void SigSEGVHandler(int) {
  log_info << "SIGSEGV received" << std::endl;
  PrintStackTrace();
  exit(1);
}

inline void SigFPEHandler(int) {
  log_info << "SIGFPE received" << std::endl;
  PrintStackTrace();
  exit(1);
}

inline void SigTERMHandler(int) {
  log_info << "SIGTERM received" << std::endl;
  PrintStackTrace();
  exit(1);
}

inline void SigINTHandler(int) {
  log_info << "SIGINT received" << std::endl;
  PrintStackTrace();
  exit(1);
}

inline void InstallSignalHandlers() {
  log_info << "Install signal handlers" << std::endl;
  signal(SIGINT, SigINTHandler);
  signal(SIGTERM, SigTERMHandler);
  signal(SIGFPE, SigFPEHandler);
  signal(SIGSEGV, SigSEGVHandler);
}