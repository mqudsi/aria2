/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2010 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "Sqlite3CookieParser.h"

#include <cstring>

#include "DlAbortEx.h"
#include "util.h"
#include "StringFormat.h"
#include "A2STR.h"
#include "cookie_helper.h"
#ifndef HAVE_SQLITE3_OPEN_V2
# include "File.h"
#endif // !HAVE_SQLITE3_OPEN_V2

namespace aria2 {

Sqlite3CookieParser::Sqlite3CookieParser(const std::string& filename):db_(0)
{
  int ret;
#ifdef HAVE_SQLITE3_OPEN_V2
  ret = sqlite3_open_v2(filename.c_str(), &db_, SQLITE_OPEN_READONLY, 0);
#else // !HAVE_SQLITE3_OPEN_V2
  if(!File(filename).isFile()) {
    return;
  }
  ret = sqlite3_open(filename.c_str(), &db_);
#endif // !HAVE_SQLITE3_OPEN_V2
  if(SQLITE_OK != ret) {
    sqlite3_close(db_);
    db_ = 0;
  }
}

Sqlite3CookieParser::~Sqlite3CookieParser()
{
  sqlite3_close(db_);
}

static std::string toString(const char* str)
{
  if(str) {
    return str;
  } else {
    return A2STR::NIL;
  }
}

static int cookieRowMapper(void* data, int rowIndex,
                           char** values, char** names)
{
  std::vector<Cookie>& cookies =
    *reinterpret_cast<std::vector<Cookie>*>(data);
  std::string cookieDomain = cookie::removePrecedingDots(toString(values[0]));
  std::string cookieName = toString(values[4]);
  std::string cookiePath = toString(values[1]);
  if(cookieName.empty() || cookieDomain.empty() ||
     !cookie::goodPath(cookiePath)) {
    return 0;
  }
  int64_t expiryTime;
  if(!util::parseLLIntNoThrow(expiryTime, toString(values[3]))) {
    return 0;
  }
  if(sizeof(time_t) == 4 && expiryTime > INT32_MAX) {
    expiryTime = INT32_MAX;
  }
  // TODO get last access, creation date(chrome only)
  Cookie c(cookieName,
           toString(values[5]), // value
           expiryTime,
           true, // persistent
           cookieDomain,
           util::isNumericHost(cookieDomain) || values[0][0] != '.', // hostOnly
           cookiePath,
           strcmp(toString(values[2]).c_str(), "1") == 0, //secure
           false,
           0 // creation time. Set this later.
           );
  cookies.push_back(c);
  return 0;
}

void Sqlite3CookieParser::parse
(std::vector<Cookie>& cookies, time_t creationTime)
{
  if(!db_) {
    throw DL_ABORT_EX(StringFormat("SQLite3 database is not opened.").str());
  }
  std::vector<Cookie> tcookies;
  char* sqlite3ErrMsg = 0;
  int ret = sqlite3_exec(db_, getQuery().c_str(), cookieRowMapper,
                         &tcookies, &sqlite3ErrMsg);
  // TODO If last access, creation date are retrieved from database,
  // following for loop must be removed.
  for(std::vector<Cookie>::iterator i = tcookies.begin(), eoi = tcookies.end();
      i != eoi; ++i) {
    (*i).setCreationTime(creationTime);
    (*i).setLastAccessTime(creationTime);
  }
  std::string errMsg;
  if(sqlite3ErrMsg) {
    errMsg = sqlite3ErrMsg;
    sqlite3_free(sqlite3ErrMsg);
  }
  if(SQLITE_OK != ret) {
    throw DL_ABORT_EX(StringFormat("Failed to read SQLite3 database: %s",
                                   errMsg.c_str()).str());
  }
  cookies.swap(tcookies);
}

} // namespace aria2