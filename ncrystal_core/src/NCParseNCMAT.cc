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

#include "NCrystal/NCParseNCMAT.hh"
#include "NCrystal/NCException.hh"
#include "NCrystal/internal/NCString.hh"
#include "NCrystal/internal/NCMath.hh"
#include <iostream>
#include <sstream>
#if __cplusplus >= 201703L
#  include <functional>//for std::invoke
#else
#  define NCPARSENCMAT_CALL_MEMBER_FN(objectaddr,ptrToMember)  ((*objectaddr).*(ptrToMember))
#endif
namespace NC = NCrystal;

namespace NCrystal {

  class NCMATParser {
  public:

    //Parse .ncmat files.

    //Parse input. Will throw BadInput exceptions in case of problems. It will
    //do some rudimentary syntax checking (including presence/absence of data
    //sections and will call NCMATData::validate), but not a full validation of
    //data (a more complete validation is typically carried out afterwards by
    //the NCMAT Loader code). It will always clear the input pointer
    //(i.e. release/close the resource).
    NCMATParser(std::unique_ptr<TextInputStream> input);
    ~NCMATParser() = default;

    NCMATData&& getData() { return std::move(m_data); }

  private:

    typedef VectS Parts;
    void parseFile( TextInputStream& );
    void parseLine( const std::string&, Parts&, unsigned linenumber ) const;
    void validateElementName(const std::string& s, unsigned lineno) const;
    double str2dbl_withfractions(const std::string&) const;

    //Section handling:
    typedef void (NCMATParser::*handleSectionDataFn)(const Parts&,unsigned);
    void handleSectionData_HEAD(const Parts&,unsigned);
    void handleSectionData_CELL(const Parts&,unsigned);
    void handleSectionData_ATOMPOSITIONS(const Parts&,unsigned);
    void handleSectionData_SPACEGROUP(const Parts&,unsigned);
    void handleSectionData_DEBYETEMPERATURE(const Parts&,unsigned);
    void handleSectionData_DYNINFO(const Parts&,unsigned);
    void handleSectionData_DENSITY(const Parts&,unsigned);
    void handleSectionData_ATOMDB(const Parts&,unsigned);
    void handleSectionData_CUSTOM(const Parts&,unsigned);

    //Collected data:
    NCMATData m_data;

    //Long vectors of data in @DYNINFO sections are kept in:
    NCMATData::DynInfo * m_active_dyninfo;
    VectD * m_dyninfo_active_vector_field;
    bool m_dyninfo_active_vector_field_allownegative;
  };

  NCMATData parseNCMATData(std::unique_ptr<TextInputStream> input, bool doFinalValidation )
  {
    NCMATParser parser(std::move(input));
    if (!doFinalValidation)
      return parser.getData();
    NCMATData data = parser.getData();
    data.validate();
    return data;
  }

}

double NC::NCMATParser::str2dbl_withfractions(const std::string& ss) const
{
if (!contains(ss,'/'))
  return str2dbl(ss);
 if (m_data.version==1)
   NCRYSTAL_THROW2(BadInput,"specification with fractions not supported in"
                   " NCMAT v1 files (offending parameter is \""<<ss<<"\")");

 VectS parts;
 split(parts,ss,0,'/');
 if (parts.size()!=2)
   NCRYSTAL_THROW2(BadInput,"multiple fractions in numbers are not supported so could not parse \""<<ss<<"\"");
 for (auto&e: parts)
   if (e.empty())
     NCRYSTAL_THROW2(BadInput,"empty denominator or numerator so could not parse \""<<ss<<"\"");
 double a = str2dbl(parts.at(0));
 double b = str2dbl(parts.at(1));
 if (ncisnan(a)||ncisnan(b)||ncisinf(a)||ncisinf(b))
   NCRYSTAL_THROW2(BadInput,"invalid division attempted in \""<<ss<<"\"");
 if (!b)
   NCRYSTAL_THROW2(BadInput,"division by zero attempted in \""<<ss<<"\"");
 return a/b;
}

NC::NCMATParser::NCMATParser( std::unique_ptr<TextInputStream> inputup)
  : m_active_dyninfo(0),
    m_dyninfo_active_vector_field(0),
    m_dyninfo_active_vector_field_allownegative(false)
{
  if (inputup==nullptr)
    NCRYSTAL_THROW2(BadInput,"NCMATParser ERROR: Invalid TextInputStream received (is nullptr)");
  TextInputStream& input = *inputup;

  //Setup source description strings first, as they are also used in error messages:
  m_data.sourceDescription = input.description();
  m_data.sourceType = input.streamType();
  {
    std::stringstream ss;
    ss << m_data.sourceType << " \"" << m_data.sourceDescription << "\"";
    m_data.sourceFullDescr = ss.str();
  }

  //Inspect first line to ensure format is NCMAT and extract version:
  std::string line;
  if ( !input.getLine(line) )
    NCRYSTAL_THROW2(BadInput,"Empty "<<m_data.sourceFullDescr);

  //First line is special, we want the file to start with "NCMAT" with no
  //whitespace in front, so we explicitly test this before invoking the more
  //generic parseLine machinery below:
  if (!startswith(line,"NCMAT"))
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" is not in NCMAT format: The first 5 characters in the first line must be \"NCMAT\"");

  //Parse first line to get file format version:
  Parts parts;
  parseLine(line,parts,1);
  if ( parts.size() == 2 ) {
    if ( parts.at(1) == "v1" ) {
      m_data.version = 1;
      if (contains(line,'#'))
        NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" has comments in the first line, which is not allowed in the NCMAT v1 format");
    } else if ( parts.at(1) == "v2" ) {
      m_data.version = 2;
    } else if ( parts.at(1) == "v3" ) {
      m_data.version = 3;
    } else {
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" is in an NCMAT format version, \""<<parts.at(1)<<"\", which is not recognised by this installation of NCrystal");
    }
  }
  if (!m_data.version)
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" is missing clear NCMAT format version designation in the first line, which should look like e.g. \"NCMAT v1\".");

  //Initial song and dance to classify source and format is now done, so proceed to parse rest of file:
  parseFile(input);

  //Unalias element names:
  m_data.unaliasElementNames();

}

void NC::NCMATParser::parseFile( TextInputStream& input )
{
  //Setup map which will be used to delegate parsing of individual sections (NB:
  //Must also update error reporting code below when adding/remove section names
  //in new format versions):
  std::map<std::string,handleSectionDataFn> section2handler;
  section2handler["HEAD"] = &NCMATParser::handleSectionData_HEAD;
  section2handler["CELL"] = &NCMATParser::handleSectionData_CELL;
  section2handler["ATOMPOSITIONS"] = &NCMATParser::handleSectionData_ATOMPOSITIONS;
  section2handler["SPACEGROUP"] = &NCMATParser::handleSectionData_SPACEGROUP;
  section2handler["DEBYETEMPERATURE"] = &NCMATParser::handleSectionData_DEBYETEMPERATURE;
  if (m_data.version>=2) {
    section2handler["DYNINFO"] = &NCMATParser::handleSectionData_DYNINFO;
    section2handler["DENSITY"] = &NCMATParser::handleSectionData_DENSITY;
  }
  if (m_data.version>=3) {
    section2handler["ATOMDB"] = &NCMATParser::handleSectionData_ATOMDB;
    section2handler["CUSTOM"] = &NCMATParser::handleSectionData_CUSTOM;
  }

  //Technically handle the part before the first section ("@SECTIONNAME") by the
  //same code as all other parts of the file, by putting it in a "HEAD" section:
  std::string current_section = "HEAD";
  std::map<std::string,handleSectionDataFn>::const_iterator itSection = section2handler.find(current_section);
  std::set<std::string> sections_seen;

  //Actual parsing (starting from the second line of input in this function):
  std::string line;
  unsigned lineno(1);
  Parts parts;
  parts.reserve(16);

  bool sawAnySection = false;
  while ( input.getLine(line) ) {

    parseLine(line,parts,++lineno);

    if (m_data.version==1 && contains(line,'#')) {
      if (sawAnySection||(!parts.empty()&&parts.at(0)[0]=='@')||line.at(0)!='#')
        NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" has comments in a place which "
                        "is not allowed in the NCMAT v1 format"" (must only appear "
                        "before the first data section and with the # marker at the"
                        " beginning of the line).");
    }

    //ignore lines which are empty or only whitespace and comments:
    if (parts.empty())
      continue;

    if (parts.at(0)[0]=='@') {
      //New section marker! First check that the syntax of this line is valid:
      sawAnySection = true;
      if (parts.size()>1)
        NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" should not have non-comment entries after a section marker"
                        " (found \""<<parts.at(1)<<"\" after \""<<parts.at(0)<<"\" in line "<<lineno<<")");
      if (line[0]!='@')
        NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" should not have whitespace before a section marker"
                        " (problem with indented \""<<parts.at(0)<<"\" in line "<<lineno<<")");

      std::string new_section(&parts.at(0)[1]);
      if (new_section.empty())
        NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" has missing section name after '@' symbol in line "<<lineno<<")");

      const bool is_custom_section = startswith(new_section,"CUSTOM_");

      //Close current section by sending it an empty parts list (and previous line number where that section ended).
      parts.clear();
#if __cplusplus >= 201703L
      std::invoke(itSection->second,*this,parts,lineno-1);
#else
      NCPARSENCMAT_CALL_MEMBER_FN(this,itSection->second)(parts,lineno);
#endif

      //Guard against repeating an existing section (unless DYNINFO or custom sections, where it is allowed)
      bool multiple_sections_allowed = ( is_custom_section || new_section=="DYNINFO" );
      if ( !multiple_sections_allowed ) {
        if (sections_seen.count(new_section) )
          NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" multiple @"<<new_section<<" sections are not allowed (line "<<lineno<<")");
        sections_seen.insert(new_section);
      }

      //Try to switch to new section
      std::swap(current_section,new_section);
      itSection = section2handler.find( is_custom_section ? "CUSTOM"_s : current_section );

      nc_assert( m_data.version>=1 && m_data.version <= 3 );
      if ( itSection == section2handler.end() ) {
        //Unsupported section name. For better error messages, first check if it
        //is due to file version:
        if (m_data.version==1 && (current_section == "DYNINFO"||current_section=="DENSITY") ) {
          NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" has @"<<current_section<<" section which is not supported in the indicated"
                          " NCMAT format version, \"NCMAT v1\". It is only available starting with \"NCMAT v2\".");
        }
        if ( m_data.version<3 && (is_custom_section||current_section=="ATOMDB") ) {
          NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" has @"<<current_section<<" section which is not supported in the indicated"
                          " NCMAT format version, \"NCMAT v"<<m_data.version<<"\". It is only available starting with \"NCMAT v3\".");
        }

        NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" has @"<<current_section<<" section which is not a supported section name.");
      }
      //Succesfully switched to the new section, proceed to next line (after
      //adding entry in customSections in case of a custom section):
      if ( is_custom_section ) {
        if ( current_section.size() <= 7 )
          NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" has @"<<current_section
                          <<" section (needs additional characters after \"CUSTOM_\").");
        m_data.customSections.emplace_back(current_section.substr(7),NCMATData::CustomSectionData());
      }
      continue;
    }

    //Line inside active section was succesfully parsed.
#if __cplusplus >= 201703L
    std::invoke(itSection->second,*this,parts,lineno);
#else
    NCPARSENCMAT_CALL_MEMBER_FN(this,itSection->second)(parts,lineno);
#endif
  }

  //End of input. Close current section by sending it an empty parts list.
  parts.clear();
#if __cplusplus >= 201703L
  std::invoke(itSection->second,*this,parts,lineno);
#else
  NCPARSENCMAT_CALL_MEMBER_FN(this,itSection->second)(parts,lineno+1);
#endif

}


void NC::NCMATParser::parseLine( const std::string& line,
                                 Parts& parts,
                                 unsigned lineno ) const
{
  //Ignore trailing comments and split line on all whitespace to return the
  //actual parts in a vector. This function is a bit like
  //line.split('#',1)[0].split() in Python, but also checks encoding which is
  //different for comments and outside comments.

  //We only allow pure ASCII in the non-comment parts, and UTF-8 in
  //comments. For the ASCII parts, we don't allow any control characters except
  //\n\r\t. For efficiency reasons, we don't currently check that the comments
  //are actually UTF-8, but we could in principle perform a few checks easily:
  //UTF-8 strings never have null bytes, and any multi-byte character will have
  //all bytes with values >=128 (i.e. the high bit is set in all bits of
  //multi-byte characters).

  //Relevant parts of ASCII byte values for our purposes:
  //
  //0-31 forbidden control chars, except \t (9), \n (10), \r (13)
  //32 : space
  //33 : ! (valid char)
  //34 : " (valid char)
  //35 : # (valid char)
  //36-126 : all valid chars
  //127-255: forbidden (127 is control char, others are not ASCII but could indicate UTF-8 multibyte char)

  parts.clear();
  const char * c = &line[0];
  const char * cE = c + line.size();
  const char * partbegin = 0;
  for (;c!=cE;++c) {
    if ( *c < 127 && ( *c > 32 && *c != '#') ) {
      //A regular character which should go in the parts vector
      if (!partbegin)
        partbegin = c;
      continue;
    }
    if ( isOneOf(*c,' ','\t') ) {
      //A whitespace character (we don't support silly stuff like vertical tabs,
      //and we kind of only grudgingly and silent accept tabs as well)
      if (partbegin) {
        parts.emplace_back(partbegin,c-partbegin);
        partbegin=0;
      }
      continue;
    }
    if ( isOneOf(*c,'\n','\r','#') ) {
      //EOL or comment begin. Only allow \r if it is in \r\n combination
      //(e.g. "DOS line endings"). A standalone \r can hide the line leading up
      //to it in printouts:
      //
      // #include <iostream>
      // int main() {
      //   std::cout<< "@CELL \n\r#comment\n..."<<std::endl<<std::endl;
      //   std::cout<< "@CELL \r\n#comment\n..."<<std::endl<<std::endl;
      //   std::cout<< "@CELL \n#comment\n..."<<std::endl<<std::endl;
      //   std::cout<< "@CELL \r#comment\n..."<<std::endl<<std::endl;
      // }
      //
      //Gives the output when run in a terminal (notice the missing @CELL in the fourth case!):
      //
      //@CELL
      //#comment
      //...
      //
      //@CELL
      //#comment
      //...
      //
      //@CELL
      //#comment
      //...
      //
      //#comment
      //...
      //
      // -> we don't really care that '\r' was used in ancient Macintosh systems...)
      // -> we also live with the fact that '\r' could still hide within comments
      //    (just too inefficient to search all comments)
      //
      if (*c=='\r') {
        if ( (c+1)!=cE && *(c+1)!='\n' ) {
          NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" contains invalid character at position "
                          <<(c-&line[0])<<" in line "<<lineno<<". Carriage return codes (aka \\r) "
                          " are not allowed unless used as part of DOS line endings.");
        }
      }

      break;
    }
    //Only reach here in case of errors:
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" contains invalid character at position "
                    <<(c-&line[0])<<" in line "<<lineno<<". Only regular ASCII characters"
                    " (including spaces) are allowed outside comments (comments can be UTF-8)");
  }
  if (partbegin) {
    //still need to add last part
    parts.emplace_back(partbegin,c-partbegin);
    partbegin=0;
  }

  //Check no illegal control codes occur in comments:
  for (;c!=cE;++c) {
    if ( ( *c>=32 && *c!=127 ) || *c < 0 )
      continue;//ok ascii or (possibly) multibyte utf-8 chars. More advanced utf-8 analysis
               //needs rather complicated code (we could do it of course...).
    if ( isOneOf(*c,'\t','\n') )
      continue;//ok
    if (*c=='\r') {
      if ( (c+1)!=cE && *(c+1)!='\n' ) {
        NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" contains invalid character at position "
                        <<(c-&line[0])<<" in line "<<lineno<<". Carriage return codes (aka \\r) "
                        " are not allowed unless used as part of DOS line endings.");
      }
      continue;
    }
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" contains illegal control code character in line "<<lineno);
  }
}

void NC::NCMATParser::handleSectionData_HEAD(const Parts& parts, unsigned lineno)
{
  if (parts.empty())
    return;
  //The HEAD pseudo-section should not have any actual contents.
  NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" should not have non-comment entries before the"
                  " first section (found \""<<parts.at(0)<<"\" in line "<<lineno<<")");
}

void NC::NCMATParser::handleSectionData_CELL(const Parts& parts, unsigned lineno)
{
  if (parts.empty()) {
    try {
      m_data.validateCell();
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,e.what()<<" (problem in the @CELL section ending in line "<<lineno<<")");
    }
    return;
  }
  if ( !isOneOf(parts.at(0),"lengths","angles") ) {
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" found \""<<parts.at(0)<<"\" where \"lengths\" or \"angles\" keyword was expected in @CELL section in line "<<lineno<<"");
  }
  if ( parts.size() != 4 ) {
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" wrong number of data entries after \""<<parts.at(0)<<"\" keyword in line "<<lineno<<" (expected three numbers)");
  }
  std::array<double,3>& targetvector = ( parts.at(0)=="lengths" ? m_data.cell.lengths : m_data.cell.angles );
  if (!(targetvector[0]==0.&&targetvector[1]==0.&&targetvector[2]==0.)) {
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" repeated keyword \""<<parts.at(0)<<"\" in line "<<lineno);
  }
  std::array<double,3> v;
  for (unsigned i = 0; i<3; ++i) {
    try {
      v[i] = str2dbl(parts.at(i+1));
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" problem while decoding \""<<parts.at(0)<<"\" parameter #"<<i+1<<" in line "<<lineno<<" : "<<e.what());
    }
  }
  targetvector = v;
  if ( targetvector[0]==0. && targetvector[1]==0. && targetvector[2]==0. ) {
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" vector \""<<parts.at(0)<<"\" is a null-vector in line "<<lineno);
  }
}

void NC::NCMATParser::validateElementName(const std::string& s, unsigned lineno) const
{
  try{
    NCMATData::validateElementNameByVersion(s,m_data.version);
  } catch (Error::BadInput&e) {
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" "<<e.what()<<" [in line "<<lineno<<"]");
  }
}

void NC::NCMATParser::handleSectionData_ATOMPOSITIONS(const Parts& parts, unsigned lineno)
{
  if (parts.empty()) {
    if (m_data.atompos.empty())
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" no element positions specified in @ATOMPOSITIONS section (expected in line "<<lineno<<")");
    try {
      m_data.validateAtomPos();
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,e.what()<<" (problem in the @ATOMPOSITIONS section ending in line "<<lineno<<")");
    }
    return;
  }
  validateElementName(parts.at(0),lineno);
  if (parts.size()!=4)
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" wrong number of data entries after element name \""<<parts.at(0)<<"\" in line "<<lineno<<" (expected three numbers)");
  std::array<double,3> v;
  for (unsigned i = 0; i<3; ++i) {
    try {
      v[i] = str2dbl_withfractions(parts.at(i+1));
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" problem while decoding position parameter #"<<i+1<<" for element \""<<parts.at(0)<<"\" in line "<<lineno<<" : "<<e.what());
    }
  }
  m_data.atompos.emplace_back(parts.at(0),v);
}

void NC::NCMATParser::handleSectionData_SPACEGROUP(const Parts& parts, unsigned lineno)
{
  if (parts.empty()) {
    if (m_data.spacegroup == 0)
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" no spacegroup number specified in @SPACEGROUP section (expected in line "<<lineno<<")");
    try {
      m_data.validateSpaceGroup();
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,e.what()<<" (problem in the @SPACEGROUP section ending in line "<<lineno<<")");
    }
    return;
  }
  if ( m_data.spacegroup != 0 || parts.size()>1 )
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" multiple entries specified in @SPACEGROUP section in line "<<lineno<<" (requires just a single number)");
  int sg(0);
  try {
    sg = str2int(parts.at(0));
  } catch (Error::BadInput&e) {
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" problem while decoding spacegroup parameter in line "<<lineno<<" : "<<e.what());
  }
  m_data.spacegroup = sg;
}

void NC::NCMATParser::handleSectionData_DEBYETEMPERATURE(const Parts& parts, unsigned lineno)
{
  if (parts.empty()) {
    if (!m_data.hasDebyeTemperature())
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" missing data in @DEBYETEMPERATURE section (expected in line "<<lineno<<")");
    try {
      m_data.validateDebyeTemperature();
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,e.what()<<" (problem in the @DEBYETEMPERATURE section ending in line "<<lineno<<")");
    }
    return;
  }

  if ( m_data.debyetemp_global != 0.0 )
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" invalid entries found after global Debye temperature was already specified (offending entries are in line "<<lineno<<")");

  if (parts.size()==1) {
    if (!m_data.debyetemp_perelement.empty())
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" invalid entries found in line "<<lineno<<" (missing element name or temperature?)");
    try {
      m_data.debyetemp_global = str2dbl(parts.at(0));
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" problem while decoding global Debye temperature in line "<<lineno<<" : "<<e.what());
    }
  } else if (parts.size()==2) {
    validateElementName(parts.at(0),lineno);
    double dt(0.0);
    try {
      dt = str2dbl(parts.at(1));
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" problem while decoding temperature for element \""<<parts.at(0)<<"\" in line "<<lineno<<" : "<<e.what());
    }
    m_data.debyetemp_perelement.emplace_back(parts.at(0),dt);
  } else {
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" wrong number of data entries in line "<<lineno);
  }
}

void NC::NCMATParser::handleSectionData_DYNINFO(const Parts& parts, unsigned lineno)
{
  const std::string& e1 = m_data.sourceFullDescr;
  if (parts.empty()) {
    if (!m_active_dyninfo)
      NCRYSTAL_THROW2(BadInput,e1<<" no input found in @DYNINFO section (expected in line "<<lineno<<")");
    try {
      m_active_dyninfo->validate();
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,e.what()<<" (problem found in the @DYNINFO section ending in line "<<lineno<<")");
    }

    //Simple validation passed. Clear and return (after squeezing memory):
    for (auto& e : m_active_dyninfo->fields)
      e.second.shrink_to_fit();

    m_dyninfo_active_vector_field = 0;
    m_dyninfo_active_vector_field_allownegative = false;
    m_active_dyninfo = 0;
    return;
  }
  if (!m_active_dyninfo) {
    m_data.dyninfos.push_back(NCMATData::DynInfo());
    m_active_dyninfo = &m_data.dyninfos.back();
  }

  //all keywords use lowercase characters + '_' and must start with two
  //lower-case letters:

  VectD * parse_target = 0;
  NCMATData::DynInfo& di = *m_active_dyninfo;
  Parts::const_iterator itParseToVect(parts.begin()), itParseToVectE(parts.end());
  const std::string& p0 = parts.at(0);

  nc_assert('A'<'a'&&'0'<'a');
  if ( p0[0] >= 'a' && contains_only(p0,"abcdefghijklmnopqrstuvwxyz_") ) {

    ////////////////////////////
    //line begins with a keyword

    if (parts.size()<2)
      NCRYSTAL_THROW2(BadInput,e1<<" provides no arguments for keyword \""<<p0<<"\" in line "<<lineno);

    m_dyninfo_active_vector_field = 0;//new keyword, deactivate active field.
    m_dyninfo_active_vector_field_allownegative = false;//forbid negative numbers except where we explicitly allow them
    ++itParseToVect;//skip keyword if later parsing values into vector
    const std::string& p1 = parts.at(1);

    if ( isOneOf(p0,"fraction","element","type") ) {
      itParseToVect = itParseToVectE;//handle argument parsing here

      /////////////////////////////////////////////////////
      //Handle common fields "fraction", "element", "type":

      if (parts.size()!=2)
        NCRYSTAL_THROW2(BadInput,e1<<" does not provide exactly one argument to keyword \""<<p0<<"\" in line "<<lineno);
      if ( ( p0 == "fraction" && di.fraction != -1.0 )
           || ( p0 == "element" && !di.element_name.empty() )
           || ( p0 == "type" && di.dyninfo_type != NCMATData::DynInfo::Undefined ) )
        NCRYSTAL_THROW2(BadInput,e1<<" keyword \""<<p0<<"\" is specified a second time in line "<<lineno);

      //Specific handling of each:
      if ( p0 == "fraction" ) {
        double fr(-1.0);
        try {
          fr = str2dbl_withfractions(p1);
        } catch (Error::BadInput&e) {
          NCRYSTAL_THROW2(BadInput,e1<<" problem while decoding fraction parameter in line "<<lineno<<" : "<<e.what());
        }
        if ( !(fr<=1.0) || !(fr>0.0) )//this also tests for NaN
          NCRYSTAL_THROW2(BadInput,e1<<" problem while decoding fraction parameter in line "<<lineno<<" (must result in a number greater than 0.0 and at most 1.0)");
        di.fraction = fr;
      } else if ( p0 == "element" ) {
        validateElementName(p1,lineno);
        di.element_name = p1;
      } else if ( p0 == "type" ) {
        if ( p1 == "scatknl" )
          di.dyninfo_type = NCMATData::DynInfo::ScatKnl;
        else if ( p1 == "vdos" )
          di.dyninfo_type = NCMATData::DynInfo::VDOS;
        else if ( p1 == "vdosdebye" )
          di.dyninfo_type = NCMATData::DynInfo::VDOSDebye;
        else if ( p1 == "freegas" )
          di.dyninfo_type = NCMATData::DynInfo::FreeGas;
        else if ( p1 == "sterile" )
          di.dyninfo_type = NCMATData::DynInfo::Sterile;
        else
          NCRYSTAL_THROW2(BadInput,e1<<" invalid @DYNINFO type specified in line "
                          <<lineno<<" (must be one of \"scatknl\", \"vdos\", \"vdosdebye\", \"freegas\", \"sterile\")");
      }
      return;
    }

    //////////////////////////////////////////////////////////////
    //Not a common field, parse into generic DynInfo::fields map :

    if ( di.fields.find(p0) != di.fields.end() )
      NCRYSTAL_THROW2(BadInput,e1<<" keyword \""<<p0<<"\" is specified a second time in line "<<lineno);

    //Setup new vector for parsing into:
    di.fields[p0] = VectD();
    parse_target = &di.fields[p0];
    parse_target->reserve(256);//will be squeezed later
    //Check if supports entry over multiple lines (mostly for keywords
    //potentially needing large number of arguments):
    if ( isOneOf(p0,"sab","sab_scaled","sqw","alphagrid","betagrid","qgrid",
                 "omegagrid","egrid","vdos_egrid", "vdos_density") ) {
      if ( isOneOf(p0,"sqw", "qgrid", "omegagrid") )
        NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" support for kernels in S(q,w) format and the keyword \""<<p0<<"\" in line "
                        <<lineno<<" is not supported in NCMAT v1 or NCMAT v2 files (but is planned for inclusion in later format versions)");
      m_dyninfo_active_vector_field = parse_target;
      m_dyninfo_active_vector_field_allownegative = (p0=="betagrid"||p0=="omegagrid");
    }

  }

  /////////////////////////////////////////////////////////////////////////////
  // Only get here if we have to parse numbers into a vector in DynInfo.fields:

  if (m_dyninfo_active_vector_field)
    parse_target = m_dyninfo_active_vector_field;

  nc_assert_always( parse_target && itParseToVect != itParseToVectE );
  std::size_t idx = (itParseToVect-parts.begin());
  std::string tmp_strcache0, tmp_strcache1;
  for (; itParseToVect!=itParseToVectE; ++itParseToVect,++idx) {
    double val;
    const std::string * srcnumstr = &(*itParseToVect);
    const std::string * srcrepeatstr = 0;
    //First check for compact notation of repeated entries:
    auto idx_repeat_marker = srcnumstr->find('r');
    if (idx_repeat_marker != std::string::npos) {
      tmp_strcache0.assign(*srcnumstr,0,idx_repeat_marker);
      tmp_strcache1.assign(*srcnumstr,idx_repeat_marker+1,srcnumstr->size()-(idx_repeat_marker+1));
      srcnumstr = &tmp_strcache0;
      srcrepeatstr = &tmp_strcache1;
    }

    unsigned repeat_count = 1;
    try {
      if (srcrepeatstr) {
        int irc = str2int(*srcrepeatstr);
        if (irc<2)
          NCRYSTAL_THROW2(BadInput,"repeated entry count parameter must be >= 2");
        repeat_count = irc;
      }
      val = str2dbl(*srcnumstr);
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,e1<<" problem while decoding vector entry #"<<1+(itParseToVect-parts.begin())<<" in line "<<lineno<<" : "<<e.what());
    }
    if (ncisnan(val)||ncisinf(val))
      NCRYSTAL_THROW2(BadInput,e1<<" problem while decoding vector entry #"<<1+(itParseToVect-parts.begin())<<" in line "<<lineno<<" : NaN or infinite number");
    if ( !m_dyninfo_active_vector_field_allownegative && val<0.0 )
      NCRYSTAL_THROW2(BadInput,e1<<" problem while decoding vector entry #"<<1+(itParseToVect-parts.begin())<<" in line "<<lineno<<" : Negative number");
    while (repeat_count--)
      parse_target->push_back(val);
  }

}

void NC::NCMATParser::handleSectionData_DENSITY(const Parts& parts, unsigned lineno)
{
  if (parts.empty()) {
    if (!m_data.density)
      NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" no input found in @DENSITY section (expected in line "<<lineno<<")");
    try {
      m_data.validateDensity();
    } catch (Error::BadInput&e) {
      NCRYSTAL_THROW2(BadInput,e.what()<<" (problem in the @DENSITY section ending in line "<<lineno<<")");
    }
    return;
  }
  if (parts.size()!=2)
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" wrong number of entries on line "<<lineno<<" in @DENSITY section");
  double density_val;
  try {
    density_val = str2dbl(parts.at(0));
  } catch (Error::BadInput&e) {
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" problem while decoding density value in line "<<lineno<<" : "<<e.what());
  }
  if (parts.at(1)=="atoms_per_aa3") {
    m_data.density_unit = NCMATData::ATOMS_PER_AA3;
    m_data.density = density_val;
  } else if (parts.at(1)=="kg_per_m3") {
    m_data.density_unit = NCMATData::KG_PER_M3;
    m_data.density = density_val;
  } else if (parts.at(1)=="g_per_cm3") {
    m_data.density_unit = NCMATData::KG_PER_M3;
    m_data.density = density_val * 1000.0;
  } else {
    NCRYSTAL_THROW2(BadInput,m_data.sourceFullDescr<<" invalid density unit in line "<<lineno);
  }

}

void NC::NCMATParser::handleSectionData_ATOMDB(const Parts& parts, unsigned lineno)
{
  if (parts.empty())
    return;//end of section, nothing to do
  if ( parts.at(0)!="nodefaults" )
    validateElementName(parts.at(0),lineno);
  m_data.atomDBLines.emplace_back(parts);
}

void NC::NCMATParser::handleSectionData_CUSTOM(const Parts& parts, unsigned)
{
  if (parts.empty())
    return;//end of section, nothing to do
  nc_assert(!m_data.customSections.empty());
  m_data.customSections.back().second.push_back(parts);
}
