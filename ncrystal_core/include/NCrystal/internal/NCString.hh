#ifndef NCrystal_String_hh
#define NCrystal_String_hh

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  This file is part of NCrystal (see https://mctools.github.io/ncrystal/)   //
//                                                                            //
//  Copyright 2015-2020 NCrystal developers                                   //
//                                                                            //
//  Licensed under the Apache License, Version 2.0 (the "License");           //
//  you may not use this file except in compliance with the License.          //
//  You may obtain a copy of the License at                                   //
//                                                                            //
//      http://www.apache.org/licenses/LICENSE-2.0                            //
//                                                                            //
//  Unless required by applicable law or agreed to in writing, software       //
//  distributed under the License is distributed on an "AS IS" BASIS,         //
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  //
//  See the License for the specific language governing permissions and       //
//  limitations under the License.                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

//String-related utilities

#include "NCrystal/NCDefs.hh"
#include <ostream>

namespace NCrystal {

  //All bytes must be in range 32..126 (plus optionally new-lines and tabs).
  bool isSimpleASCII(const std::string&, bool allow_tab=false, bool allow_newline = false);

  //Strip excess whitespace (" \t\r\n") from both ends of string:
  void trim( std::string& );

  //Split input string on separator (default sep=0 means splitting on general
  //whitespace - " \t\r\n"). Results are placed in output vector, which is first
  //cleared of existing contents. Empty parts are only kept when sep!=0 (similar
  //to pythons str.split()). Finally, maxsplit can be used to limit the number
  //of splittings performed:
  void split(VectS& output,
             const std::string& input,
             std::size_t maxsplit = 0,
             char sep = 0 );

  //Get basename and extension from filename:
  std::string basename(const std::string& filename);
  std::string getfileext(const std::string& filename);

  bool startswith(const std::string& str, const std::string& substr);
  bool endswith(const std::string& str, const std::string& substr);

  //Check if given char or substring (needle) is present in a string (haystack):
  bool contains(const std::string& haystack, char needle );
  bool contains(const std::string& haystack, const std::string& needle);
  //Check if any of the chars in "needles" is present in the string (haystack):
  bool contains_any(const std::string& haystack, const std::string& needles);
  //Check if "haystack" consists entirely of chars from string needles:
  bool contains_only(const std::string& haystack, const std::string& needles);

  //Convert strings to numbers. In case of problems, a BadInput exception will
  //be thrown (provide err to modify the message in that exception):
  double str2dbl(const std::string&, const char * errmsg = 0);
  int str2int(const std::string&, const char * errmsg = 0);

  //Versions which don't throw:
  bool safe_str2dbl(const std::string&, double& result );
  bool safe_str2int(const std::string&, int& result );

  //Convenience:
  inline bool isDouble( const std::string& ss ) { double dummy; return safe_str2dbl(ss,dummy); }
  inline bool isInt( const std::string& ss ) { int dummy; return safe_str2int(ss,dummy); }

  //How many digits does string end with (e.g. 1 in "H1", 0 in "H1a", 3 in "Bla123".
  unsigned countTrailingDigits( const std::string& ss );

  //"Bla123" => ("Bla","123"). Special cases: "Bla" -> ("Bla","") "Bla012" -> ("Bla","012")
  std::pair<std::string,std::string> decomposeStrWithTrailingDigits( const std::string& ss );

  //Replace all occurances of oldtxt in str with newtxt:
  void strreplace(std::string& str, const std::string& oldtxt, const std::string& newtxt);

  //["a","bb","123"] -> "a bb 123":
  std::string joinstr(const VectS& parts, std::string separator = " ");

  //Pretty-prints a value. If detectSimpleRationalNumbers detects a simple
  //fraction, it will be printed as e.g. "2/9", or "3" (in case of integers). If
  //not, it will be printed as a floating point (with a particular precision in
  //case prec!=0):
  void prettyPrintValue(std::ostream& os, double value, unsigned prec=0 );
  std::string prettyPrintValue2Str(double value, unsigned prec=0 );

}

#endif
