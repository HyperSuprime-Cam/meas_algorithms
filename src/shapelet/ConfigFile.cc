// -*- LSST-C++ -*-

/* 
 * LSST Data Management System
 * Copyright 2008, 2009, 2010 LSST Corporation.
 * 
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the LSST License Statement and 
 * the GNU General Public License along with this program.  If not, 
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */

#include "lsst/meas/algorithms/shapelet/ConfigFile.h"

namespace lsst {
namespace meas {
namespace algorithms {
namespace shapelet {

    ConfigFile::ConfigFile() :
        _delimiter("="), _comment("#"), _include("+"), _sentry("EndConfigFile")
    {
        // Construct an empty ConfigFile
    }

    ConfigFile::ConfigFile( 
        std::string fileName, std::string delimiter,
        std::string comment, std::string inc, std::string sentry ) : 
        _delimiter(delimiter), _comment(comment), _include(inc), _sentry(sentry)
    {
        // Construct a ConfigFile, getting keys and values from given file

        std::ifstream in( fileName.c_str() );

        if( !in ) {
#ifdef NOTHROW
            std::cerr<<"File not found: "<<fileName<<std::endl; 
            exit(1); 
#else
            throw FileNotFoundException(fileName);
#endif
        }

        in >> (*this);
    }

    void ConfigFile::load( 
        std::string fileName, std::string delimiter,
        std::string comment, std::string inc, std::string sentry )
    {
        // Construct a ConfigFile, getting keys and values from given file

        std::string delimiter1 = _delimiter;
        std::string comment1 = _comment;
        std::string inc1 = _include;
        std::string sentry1 = _sentry;

        if (delimiter != "") _delimiter = delimiter;
        if (comment != "") _comment = comment;
        if (inc != "") _include = inc;
        if (sentry != "") _sentry = sentry;

        std::ifstream in( fileName.c_str() );

        if( !in ) {
#ifdef NOTHROW
            std::cerr<<"File not found: "<<fileName<<std::endl; 
            exit(1); 
#else
            throw FileNotFoundException(fileName);
#endif
        }

        in >> (*this);

        _delimiter = delimiter1;
        _comment = comment1;
        _include = inc1;
        _sentry = sentry1;
    }

    ConvertibleString& ConfigFile::getNoCheck( const std::string& key )
    {
        std::string key2 = key;
        trim(key2);
        return _contents[key2]; 
    }

    ConvertibleString ConfigFile::get( const std::string& key ) const
    {
        std::string key2 = key;
        trim(key2);
        MapCIt p = _contents.find(key2);
        if (p == _contents.end()) {
#ifdef NOTHROW
            std::cerr<<"Key not found: "<<key2<<std::endl; 
            exit(1); return key; 
#else
            throw ParameterException(
                "ConfigFile error: key "+key2+" not found");
#endif
        } else {
            return p->second;
        }
    }

    void ConfigFile::remove( const std::string& key )
    {
        // Remove key and its value
        _contents.erase( _contents.find( key ) );
        return;
    }


    bool ConfigFile::keyExists( const std::string& key ) const
    {
        // Indicate whether key is found
        MapCIt p = _contents.find( key );
        return ( p != _contents.end() );
    }


    void ConfigFile::trim( std::string& s )
    {
        // Remove leading and trailing whitespace
        std::string whiteSpace = " \n\t\v\r\f";
        s.erase( 0, s.find_first_not_of(whiteSpace) );
        s.erase( s.find_last_not_of(whiteSpace) + 1);
    }


    void ConfigFile::write(std::ostream& os) const
    {
        // Save a ConfigFile to os
        for(MapCIt p = _contents.begin(); p != _contents.end(); ++p ) {
            os << p->first << " " << _delimiter << " ";
            os << p->second << std::endl;
        }
    }

    void ConfigFile::writeAsComment(std::ostream& os) const
    {
        // Save a ConfigFile to os
        for(MapCIt p = _contents.begin(); p != _contents.end(); ++p ) {
            std::string f = p->first;
            std::string s = p->second;
            std::replace(f.begin(),f.end(),'\n',' ');
            std::replace(s.begin(),s.end(),'\n',' ');
            os << _comment << " " << f << " " << _delimiter << " ";
            os << s << std::endl;
        }
    }

    void ConfigFile::read(std::istream& is)
    {
        // Load a ConfigFile from is
        // Read in keys and values, keeping internal whitespace
        const std::string& delim = _delimiter;  // separator
        const std::string& comm = _comment;    // comment
        const std::string& inc = _include;      // include directive
        const std::string& sentry = _sentry;     // end of file sentry
        const std::string::size_type skip = delim.size(); // length of separator

        std::string nextLine = "";  
        // might need to read ahead to see where value ends

        while( is || nextLine.size() > 0 ) {
            // Read an entire line at a time
            std::string line;
            if( nextLine.size() > 0 ) {
                line = nextLine;  // we read ahead; use it now
                nextLine = "";
            } else {
                std::getline( is, line );
            }

            // Ignore comments
            line = line.substr( 0, line.find(comm) );

            // Remove leading and trailing whitespace
            trim(line);

            // If line is blank, go on to next line.
            if (line.size() == 0) continue;

            // Check for include directive (only at start of line)
            if (line.find(inc) == 0) { 
                line.erase(0,inc.size());
                std::stringstream ss(line);
                std::string fileName;
                ss >> fileName;
                load(fileName);
                // implcitly skip the rest of the line.
                continue;
            }

            // Check for end of file sentry
            if( sentry != "" && line.find(sentry) != std::string::npos ) return;

            // Parse the line if it contains a delimiter
            std::string::size_type delimPos = line.find( delim );
            if( delimPos < std::string::npos ) {
                // Extract the key
                std::string key = line.substr( 0, delimPos );
                line.replace( 0, delimPos+skip, "" );

                // See if value continues on the next line
                // Stop at blank line, next line with a key, end of stream,
                // or end of file sentry
                bool terminate = false;
                while( !terminate && is ) {
                    std::getline( is, nextLine );
                    terminate = true;

                    std::string nextLineCopy = nextLine;
                    ConfigFile::trim(nextLineCopy);
                    if( nextLineCopy == "" ) continue;

                    nextLine = nextLine.substr( 0, nextLine.find(comm) );
                    if( nextLine.find(delim) != std::string::npos )
                        continue;
                    if( sentry != "" && nextLine.find(sentry) != std::string::npos )
                        continue;

                    nextLineCopy = nextLine;
                    ConfigFile::trim(nextLineCopy);
                    if( nextLineCopy != "" ) line += "\n";
                    line += nextLine;
                    terminate = false;
                }

                // Store key and value
                ConfigFile::trim(key);
                ConfigFile::trim(line);
                _contents[key] = line;  // overwrites if key is repeated
            }
        }
    }

}}}}
