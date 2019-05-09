
#ifndef _PARSER_H_
#define _PARSER_H_

#include "const.h"
#include <fstream>
#include <string>
#include "string.h"
#include <sstream>
#include <algorithm>
#include "Domain.h"
#include "FileIO.h"
#include "Parallel.h"


class Parser {

public:

  void readParamsFile(std::string fNameIn, Domain &dom, Parallel &par, FileIO &io) {

    std::string strTimeMethod;

    // Initialize all read-in values to -999
    dom.nx_glob   = 0;
    dom.ny_glob   = 0;
    dom.xlen      = -999;
    dom.ylen      = -999;
    dom.cfl       = -999;
    dom.simLength = -999;
    par.nproc_x   = -999;
    par.nproc_y   = -999;
    outFreq       = -999;
    doWeno        = -999;
    timeMethod    = -999;

    // Read in colon-separated key: value file line by line
    std::ifstream fInStream(fNameIn);
    std::string line;
    while (std::getline(fInStream, line)) {
      // Remove spaces and tabs from the line
      line.erase (std::remove(line.begin(), line.end(), ' '), line.end());
      line.erase (std::remove(line.begin(), line.end(), '\t'), line.end());

      // If the line isn't empty and doesn't begin with a comment specifier, split it based on the colon
      if (!line.empty() && line.find("//",0) != 0) {
        // Find the colon
        uint splitloc = line.find(':',0);
        // Store the key and value strings
        std::string key   = line.substr(0,splitloc);
        std::string value = line.substr(splitloc+1,line.length()-splitloc);

        // Transform the value into a string stream for convenience
        std::stringstream ssVal(value);

        // Match the key, and store the value
        if      ( !strcmp( "nx"        , key.c_str() ) ) { ssVal >> dom.nx_glob  ; }
        else if ( !strcmp( "ny"        , key.c_str() ) ) { ssVal >> dom.ny_glob  ; }
        else if ( !strcmp( "xlen"      , key.c_str() ) ) { ssVal >> dom.xlen     ; }
        else if ( !strcmp( "ylen"      , key.c_str() ) ) { ssVal >> dom.ylen     ; }
        else if ( !strcmp( "cfl"       , key.c_str() ) ) { ssVal >> dom.cfl      ; }
        else if ( !strcmp( "simLength" , key.c_str() ) ) { ssVal >> dom.simLength; }
        else if ( !strcmp( "parNx"     , key.c_str() ) ) { ssVal >> par.nproc_x  ; }
        else if ( !strcmp( "parNy"     , key.c_str() ) ) { ssVal >> par.nproc_y  ; }
        else if ( !strcmp( "outFreq"   , key.c_str() ) ) { ssVal >> outFreq      ; }
        else if ( !strcmp( "doWeno"    , key.c_str() ) ) { ssVal >> doWeno       ; }
        else if ( !strcmp( "timeMethod", key.c_str() ) ) { ssVal >> strTimeMethod; handleTimeMethod(strTimeMethod,fNameIn); }
        else {
          std::cout << "Error: key " << key << " not understood in file " << fNameIn << "\n";
          exit(-1);
        }
      }
    }

    // Test to make sure all values were initialized
    if (dom.nx_glob   == 0   ) { std::cout << "Error: key " << "nx"        << " not set."; exit(-1); }
    if (dom.ny_glob   == 0   ) { std::cout << "Error: key " << "ny"        << " not set."; exit(-1); }
    if (dom.xlen      == -999) { std::cout << "Error: key " << "xlen"      << " not set."; exit(-1); }
    if (dom.ylen      == -999) { std::cout << "Error: key " << "ylen"      << " not set."; exit(-1); }
    if (dom.cfl       == -999) { std::cout << "Error: key " << "cfl"       << " not set."; exit(-1); }
    if (dom.simLength == -999) { std::cout << "Error: key " << "simLength" << " not set."; exit(-1); }
    if (par.nproc_x   == -999) { std::cout << "Error: key " << "parNx"     << " not set."; exit(-1); }
    if (par.nproc_y   == -999) { std::cout << "Error: key " << "parNy"     << " not set."; exit(-1); }
    if (outFreq       == -999) { std::cout << "Error: key " << "outFreq"   << " not set."; exit(-1); }
    if (doWeno        == -999) { std::cout << "Error: key " << "doWeno"    << " not set."; exit(-1); }

    // Print out the values
    if (par.masterproc) {
      std::cout << "nx: "         << dom.nx_glob   << "\n";
      std::cout << "ny: "         << dom.ny_glob   << "\n";
      std::cout << "xlen: "       << dom.xlen      << "\n";
      std::cout << "ylen: "       << dom.ylen      << "\n";
      std::cout << "cfl: "        << dom.cfl       << "\n";
      std::cout << "simLength: "  << dom.simLength << "\n";
      std::cout << "parNx: "      << par.nproc_x   << "\n";
      std::cout << "parNy: "      << par.nproc_y   << "\n";
      std::cout << "outFreq: "    << outFreq       << "\n";
      std::cout << "doWeno: "     << doWeno        << "\n";
      std::cout << "timeMethod: " << timeMethod    << "\n";
    }

  }

  void handleTimeMethod(std::string &str, std::string &fNameIn) {
    size_t splitloc = str.find("//",0);
    std::string strloc;
    if (splitloc != std::string::npos){
      strloc = str.substr(0,splitloc);
    } else {
      strloc = str;
    }
    if      ( !strcmp(strloc.c_str(),"SSPRK3") ) { timeMethod = TIME_SSPRK3; }
    else if ( !strcmp(strloc.c_str(),"ADER"  ) ) { timeMethod = TIME_ADER  ; }
    else  {
      std::cout << "Error: unrecognized timeMethod " << str << " in file " << fNameIn << "\n";
      exit(-1);
    }
  }

};

#endif
