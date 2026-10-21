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

/* Changed for the Windows port in casement-plugin-mosh, October 2026: the
   user's terminal is an STMFrontend (Casement's terminal view) instead of
   the process's tty, so there is no termios, no window size ioctl and no
   signals: input, size changes and the request to quit arrive as frontend
   events. The window size is given to the constructor, and shutdown()
   returns the message mosh-client would print instead of printing it.
   Messages are UTF-32 strings, as wchar_t has 16 bits on Windows. */

#ifndef STM_CLIENT_HPP
#define STM_CLIENT_HPP

#include <deque>
#include <stdexcept>
#include <string>

#include "completeterminal.h"
#include "networktransport.h"
#include "user.h"
#include "shared.h"
#include "terminaloverlay.h"

/* The user's side: what mosh-client reads from and writes to its tty. */
class STMFrontend {
public:
  enum EventType {
    INPUT,     /* bytes the user's terminal produced */
    RESIZE,    /* the terminal has a new size */
    SHUTDOWN   /* end the session, as SIGTERM does */
  };

  struct Event {
    EventType type;
    std::string bytes;
    int width, height;
  };

  virtual ~STMFrontend() {}

  /* Bytes for the user's terminal. */
  virtual void write( const std::string &bytes ) = 0;

  /* An event object that is signalled when events are waiting. */
  virtual HANDLE event( void ) = 0;

  /* The events that arrived since the last call, oldest first. */
  virtual std::deque< Event > take_events( void ) = 0;
};

class STMClient {
private:
  std::string ip;
  std::string port;
  std::string key;

  int escape_key;
  int escape_pass_key;
  int escape_pass_key2;
  bool escape_requires_lf;
  std::u32string escape_key_help;

  STMFrontend &frontend;
  int window_width, window_height;

  Terminal::Framebuffer local_framebuffer, new_state;
  Overlay::OverlayManager overlays;
  typedef Network::Transport< Network::UserStream, Terminal::Complete > NetworkType;
  typedef shared::shared_ptr< NetworkType > NetworkPointer;
  NetworkPointer network;
  Terminal::Display display;

  std::u32string connecting_notification;
  bool repaint_requested, lf_entered, quit_sequence_started;
  bool clean_shutdown;
  unsigned int verbose;

  void main_init( void );
  void process_network_input( void );
  bool process_user_input( const std::string &bytes );
  bool process_resize( int width, int height );

  void output_new_frame( void );

public:
  STMClient( STMFrontend &s_frontend, int s_width, int s_height,
             const char *s_ip, const char *s_port, const char *s_key, const char *predict_mode, unsigned int s_verbose, const char *predict_overwrite )
    : ip( s_ip ? s_ip : "" ), port( s_port ? s_port : "" ),
    key( s_key ? s_key : "" ),
    escape_key( 0x1E ), escape_pass_key( '^' ), escape_pass_key2( '^' ),
    escape_requires_lf( false ), escape_key_help( U"?" ),
      frontend( s_frontend ),
      window_width( s_width ), window_height( s_height ),
      local_framebuffer( 1, 1 ),
      new_state( 1, 1 ),
      overlays(),
      network(),
      display( true ), /* fixed xterm-256color capabilities */
      connecting_notification(),
      repaint_requested( false ),
      lf_entered( false ),
      quit_sequence_started( false ),
      clean_shutdown( false ),
      verbose( s_verbose )
  {
    if ( predict_mode ) {
      if ( !strcmp( predict_mode, "always" ) ) {
	overlays.get_prediction_engine().set_display_preference( Overlay::PredictionEngine::Always );
      } else if ( !strcmp( predict_mode, "never" ) ) {
	overlays.get_prediction_engine().set_display_preference( Overlay::PredictionEngine::Never );
      } else if ( !strcmp( predict_mode, "adaptive" ) ) {
	overlays.get_prediction_engine().set_display_preference( Overlay::PredictionEngine::Adaptive );
      } else if ( !strcmp( predict_mode, "experimental" ) ) {
	overlays.get_prediction_engine().set_display_preference( Overlay::PredictionEngine::Experimental );
      } else {
	throw std::invalid_argument( std::string( "Unknown prediction mode " ) + predict_mode );
      }
    }
    if ( predict_overwrite && !strcmp( predict_overwrite, "yes" ) ) {
      overlays.get_prediction_engine().set_predict_overwrite( true );
    } 
  }

  void init( void );
  /* Restores the user's terminal and returns what mosh-client would print
     on the way out (empty after a clean shutdown). */
  std::string shutdown( void );
  bool main( void );

  bool still_connecting( void ) const
  {
    /* Initially, network == NULL */
    return network && ( network->get_remote_state_num() == 0 );
  }

  /* The server ended the session (its shell exited) and we acknowledged. */
  bool server_ended( void ) const
  {
    return network && network->counterparty_shutdown_ack_sent();
  }

  /* unused */
  STMClient( const STMClient & );
  STMClient & operator=( const STMClient & );
};

#endif
