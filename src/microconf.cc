/* MicroConf - minimal configuration framework
 * Copyright (C) 2010 Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <stdlib.h>
#include "microconf.hh"

using std::string;
using std::vector;

MicroConf::MicroConf (const string& filename)
{
  cfg_file = fopen (filename.c_str(), "r");

  current_file = filename;
  current_no = 0;
}

static bool
is_newline (char ch)
{
  return (ch == '\n' || ch == '\r');
}

bool
MicroConf::next()
{
  char s[1024];

  if (!fgets (s, 1024, cfg_file))
    return false; // eof

  current_line = s;
  current_no++;

  // strip newline at end of input
  while (!current_line.empty() && is_newline (current_line[current_line.size() - 1]))
    current_line.resize (current_line.size() - 1);

  tokenize();

  return true;
}

string
MicroConf::line()
{
  return current_line;
}

bool
string_chars (char ch)
{
  if ((ch >= 'A' && ch <= 'Z')
  ||  (ch >= '0' && ch <= '9')
  ||  (ch >= 'a' && ch <= 'z')
  ||  (ch == '.')
  ||  (ch == '/')
  ||  (ch == '-')
  ||  (ch == '_'))
    return true;

  return false;
}

bool
white_space (char ch)
{
  return (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r');
}

void
MicroConf::tokenize()
{
  enum { BLANK, STRING, COMMENT } state = BLANK;
  string s;

  string xline = current_line + '\n';
  tokens.clear();
  for (string::const_iterator i = xline.begin(); i != xline.end(); i++)
    {
      if (state == BLANK && string_chars (*i))
        {
          state = STRING;
          s += *i;
        }
      else if (state == STRING && string_chars (*i))
        {
          s += *i;
        }
      else if (state == STRING && white_space (*i))
        {
          tokens.push_back (s);
          s = "";
          state = BLANK;
        }
      else if (*i == '#')
        {
          state = COMMENT;
        }
    }
}

bool
MicroConf::convert (const std::string& token, int& arg)
{
  arg = atoi (token.c_str());
  return true;
}

bool
MicroConf::convert (const std::string& token, double& arg)
{
  arg = atof (token.c_str());
  return true;
}

bool
MicroConf::convert (const std::string& token, std::string& arg)
{
  arg = token;
  return true;
}

void
MicroConf::die_if_unknown()
{
  if (tokens.size())
    {
      fprintf (stderr, "configuration file %s: bad line number %d: %s\n",
               current_file.c_str(), current_no, current_line.c_str());
      exit (1);
    }
}