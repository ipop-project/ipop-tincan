/*
* ipop-project
* Copyright 2016, University of Florida
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/
#define TINCAN_MAIN 1
#include "tincan_base.h"
#include "tincan.h"
using namespace tincan;

bool TincanParameters::kVersionCheck = false;
bool TincanParameters::kNeedsHelp = false;

Tincan * Tincan::self_= NULL;
TincanParameters tp;

int main(int argc, char **argv)
{
  int rv = 0;
  try {
    tp.ParseCmdlineArgs(argc, argv);
    if(tp.kVersionCheck) {
      cout << kTincanVerMjr << "." 
        << kTincanVerMnr << "." 
        << kTincanVerRev << endl;
    }
    else if(tp.kNeedsHelp) {
      std::cout << "-v         Version check.\n" <<
        "-p=[num]   Specify tincan control port" << endl;
    }
    else {
      rtc::LogMessage::LogToDebug(rtc::LS_INFO); //default, can be modified by contoller
      Tincan tc;
      tc.Run();
    }
  }
  catch(exception & e) {
    rv = -1;
    LOG_F(LS_ERROR) << e.what();
  }
  return rv;
}
