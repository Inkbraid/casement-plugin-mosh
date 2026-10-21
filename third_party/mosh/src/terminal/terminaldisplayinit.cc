/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

/* Changed for the Windows port in casement-plugin-mosh, October 2026:
   Windows has no terminfo. The terminal is always Casement's terminal view,
   which emulates xterm-256color, so the capabilities are fixed to that
   entry's (ncurses 6.4): ech=\E[%p1%dX, bce, smcup=\E[?1049h\E[22;0;0t,
   rmcup=\E[?1049l\E[23;0;0t; and xterm can set the window title. */

#include "config.h"
#include "terminaldisplay.h"

#include <stdlib.h>

using namespace Terminal;

Display::Display( bool use_environment )
  : has_ech( true ), has_bce( true ), has_title( true ), smcup( NULL ), rmcup( NULL )
{
  if ( use_environment ) {
    if ( !getenv( "MOSH_NO_TERM_INIT" ) ) {
      smcup = "\033[?1049h\033[22;0;0t";
      rmcup = "\033[?1049l\033[23;0;0t";
    }
  }
}
