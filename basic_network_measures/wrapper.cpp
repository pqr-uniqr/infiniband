#include <vector>
#include <iostream>
#include "header.h"
#include "IntelPCM/cpucounters.h"

using namespace std;

SystemCounterState before, after;
PCM *m;

extern "C" void pcm_setup_generic(){
  m = PCM::getInstance();

  PCM::ErrorCode status;

  status = m->program();

  if (status != PCM::Success){
    cout << "error code: " << status << endl;
    cout << "Could not program PCM" << endl;
    exit(1);
  }
}

extern "C" void pcm_lap(int isBefore){
  if (isBefore) {
    before = getSystemCounterState();
  } else {
    after = getSystemCounterState();
  }
}

extern "C" double pcm_measure(){
  m->cleanup();
  return getCycles(before, after);
}
