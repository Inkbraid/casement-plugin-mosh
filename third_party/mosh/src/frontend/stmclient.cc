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

/* Changed for the Windows port in casement-plugin-mosh, October 2026: see
   stmclient.h. The main loop waits with WaitForMultipleObjects() on an
   event that every UDP socket signals (WSAEventSelect) and on the
   frontend's event, and finds the readable sockets with WSAPoll(), where
   mosh-client used pselect() on the sockets, stdin and signals. There is
   no suspend (Ctrl-Z after the escape key): the plugin has no shell to
   return to. */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include <winsock2.h>
#include <windows.h>

#include "stmclient.h"
#include "completeterminal.h"
#include "user.h"
#include "fatal_assert.h"
#include "timestamp.h"
#include "unicode.h"

#include "networktransport-impl.h"

using std::u32string;

static u32string u32( const std::string &s )
{
  return mosh_utf8_to_u32( s );
}

void STMClient::init( void )
{
  /* Put terminal in application-cursor-key mode */
  frontend.write( display.open() );

  /* Add our name to window title */
  if ( !getenv( "MOSH_TITLE_NOPREFIX" ) ) {
    overlays.set_title_prefix( u32string( U"[mosh] " ) );
  }

  /* Set terminal escape key. */
  const char *escape_key_env;
  if ( (escape_key_env = getenv( "MOSH_ESCAPE_KEY" )) != NULL ) {
    if ( strlen( escape_key_env ) == 1 ) {
      escape_key = (int)escape_key_env[0];
      if ( escape_key > 0 && escape_key < 128 ) {
	if ( escape_key < 32 ) {
	  /* If escape is ctrl-something, pass it with repeating the key without ctrl. */
	  escape_pass_key = escape_key + (int)'@';
	} else {
	  /* If escape is something else, pass it with repeating the key itself. */
	  escape_pass_key = escape_key;
	}
	if ( escape_pass_key >= 'A' && escape_pass_key <= 'Z' ) {
	  /* If escape pass is an upper case character, define optional version
	     as lower case of the same. */
	  escape_pass_key2 = escape_pass_key + (int)'a' - (int)'A';
	} else {
	  escape_pass_key2 = escape_pass_key;
	}
      } else {
	escape_key = 0x1E;
	escape_pass_key = '^';
	escape_pass_key2 = '^';
      }
    } else if ( strlen( escape_key_env ) == 0 ) {
      escape_key = -1;
    } else {
      escape_key = 0x1E;
      escape_pass_key = '^';
      escape_pass_key2 = '^';
    }
  } else {
    escape_key = 0x1E;
    escape_pass_key = '^';
    escape_pass_key2 = '^';
  }

  /* There are so many better ways to shoot oneself into leg than
     setting escape key to Ctrl-C, Ctrl-D, NewLine, Ctrl-L or CarriageReturn
     that we just won't allow that. */
  if ( escape_key == 0x03 || escape_key == 0x04 || escape_key == 0x0A || escape_key == 0x0C || escape_key == 0x0D ) {
    escape_key = 0x1E;
    escape_pass_key = '^';
    escape_pass_key2 = '^';
  }

  /* Adjust escape help differently if escape is a control character. */
  if ( escape_key > 0 ) {
    char escape_pass_name_buf[16];
    char escape_key_name_buf[16];
    snprintf(escape_pass_name_buf, sizeof escape_pass_name_buf, "\"%c\"", escape_pass_key);
    if (escape_key < 32) {
      snprintf(escape_key_name_buf, sizeof escape_key_name_buf, "Ctrl-%c", escape_pass_key);
      escape_requires_lf = false;
    } else {
      snprintf(escape_key_name_buf, sizeof escape_key_name_buf, "\"%c\"", escape_key);
      escape_requires_lf = true;
    }
    string tmp;
    tmp = string( escape_pass_name_buf );
    u32string escape_pass_name = u32( tmp );
    tmp = string( escape_key_name_buf );
    u32string escape_key_name = u32( tmp );
    escape_key_help = U"Commands: \".\" quits, " + escape_pass_name + U" gives literal " + escape_key_name;
    overlays.get_notification_engine().set_escape_key_string( tmp );
  }
  connecting_notification = u32( "Nothing received from server on UDP port " + port + "." );
}

std::string STMClient::shutdown( void )
{
  /* Restore screen state */
  overlays.get_notification_engine().set_notification_string( u32string( U"" ) );
  overlays.get_notification_engine().server_heard( timestamp() );
  overlays.set_title_prefix( u32string( U"" ) );
  output_new_frame();

  /* Restore terminal state */
  frontend.write( display.close() );

  if ( still_connecting() ) {
    return "mosh did not make a successful connection to " + ip + ":" + port + ".\n"
      "Please verify that UDP port " + port + " is not firewalled and can reach the server.\n\n"
      "(By default, mosh uses a UDP port between 60000 and 61000. The -p option\n"
      "selects a specific UDP port number.)\n";
  } else if ( network && !clean_shutdown ) {
    return "mosh did not shut down cleanly. Please note that the\n"
      "mosh-server process may still be running on the server.\n";
  }
  return std::string();
}

void STMClient::main_init( void )
{
  /* local state */
  local_framebuffer = Terminal::Framebuffer( window_width, window_height );
  new_state = Terminal::Framebuffer( 1, 1 );

  /* initialize screen */
  string init = display.new_frame( false, local_framebuffer, local_framebuffer );
  frontend.write( init );

  /* open network */
  Network::UserStream blank;
  Terminal::Complete local_terminal( window_width, window_height );
  network = NetworkPointer( new NetworkType( blank, local_terminal, key.c_str(), ip.c_str(), port.c_str() ) );

  network->set_send_delay( 1 ); /* minimal delay on outgoing keystrokes */

  /* tell server the size of the terminal */
  network->get_current_state().push_back( Parser::Resize( window_width, window_height ) );

  /* be noisy as necessary */
  network->set_verbose( verbose );
}

void STMClient::output_new_frame( void )
{
  if ( !network ) { /* clean shutdown even when not initialized */
    return;
  }

  /* fetch target state */
  new_state = network->get_latest_remote_state().state.get_fb();

  /* apply local overlays */
  overlays.apply( new_state );

  /* calculate minimal difference from where we are */
  const string diff( display.new_frame( !repaint_requested,
					local_framebuffer,
					new_state ) );
  if ( !diff.empty() ) {
    frontend.write( diff );
  }

  repaint_requested = false;

  local_framebuffer = new_state;
}

void STMClient::process_network_input( void )
{
  network->recv();
  
  /* Now give hints to the overlays */
  overlays.get_notification_engine().server_heard( network->get_latest_remote_state().timestamp );
  overlays.get_notification_engine().server_acked( network->get_sent_state_acked_timestamp() );

  overlays.get_prediction_engine().set_local_frame_acked( network->get_sent_state_acked() );
  overlays.get_prediction_engine().set_send_interval( network->send_interval() );
  overlays.get_prediction_engine().set_local_frame_late_acked( network->get_latest_remote_state().state.get_echo_ack() );
}

bool STMClient::process_user_input( const std::string &bytes )
{
  const char *buf = bytes.data();
  const int bytes_read = static_cast<int>( bytes.size() );

  NetworkType &net = *network;

  if ( net.shutdown_in_progress() ) {
    return true;
  }
  overlays.get_prediction_engine().set_local_frame_sent( net.get_sent_state_last() );

  /* Don't predict for bulk data. */
  bool paste = bytes_read > 100;
  if ( paste ) {
    overlays.get_prediction_engine().reset();
  }

  for ( int i = 0; i < bytes_read; i++ ) {
    char the_byte = buf[ i ];

    if ( !paste ) {
      overlays.get_prediction_engine().new_user_byte( the_byte, local_framebuffer );
    }

    if ( quit_sequence_started ) {
      if ( the_byte == '.' ) { /* Quit sequence is Ctrl-^ . */
	if ( net.has_remote_addr() && (!net.shutdown_in_progress()) ) {
	  overlays.get_notification_engine().set_notification_string( u32string( U"Exiting on user request..." ), true );
	  net.start_shutdown();
	  return true;
	}
	return false;
      } else if ( (the_byte == escape_pass_key) || (the_byte == escape_pass_key2) ) {
	/* Emulation sequence to type escape_key is escape_key +
	   escape_pass_key (that is escape key without Ctrl) */
	net.get_current_state().push_back( Parser::UserByte( escape_key ) );
      } else {
	/* Escape key followed by anything other than . and ^ gets sent literally */
	net.get_current_state().push_back( Parser::UserByte( escape_key ) );
	net.get_current_state().push_back( Parser::UserByte( the_byte ) );	  
      }

      quit_sequence_started = false;

      if ( overlays.get_notification_engine().get_notification_string() == escape_key_help ) {
	overlays.get_notification_engine().set_notification_string( U"" );
      }

      continue;
    }

    quit_sequence_started = (escape_key > 0) && (the_byte == escape_key) && (lf_entered || (! escape_requires_lf));
    if ( quit_sequence_started ) {
      lf_entered = false;
      overlays.get_notification_engine().set_notification_string( escape_key_help, true, false );
      continue;
    }

    lf_entered = ( (the_byte == 0x0A) || (the_byte == 0x0D) ); /* LineFeed, Ctrl-J, '\n' or CarriageReturn, Ctrl-M, '\r' */

    if ( the_byte == 0x0C ) { /* Ctrl-L */
      repaint_requested = true;
    }

    net.get_current_state().push_back( Parser::UserByte( the_byte ) );		
  }

  return true;
}

bool STMClient::process_resize( int width, int height )
{
  /* get new size */
  if ( width <= 0 || height <= 0 ) {
    return true;
  }
  window_width = width;
  window_height = height;
  
  /* tell remote emulator */
  Parser::Resize res( window_width, window_height );
  
  if ( !network->shutdown_in_progress() ) {
    network->get_current_state().push_back( res );
  }

  /* note remote emulator will probably reply with its own Resize to adjust our state */
  
  /* tell prediction engine */
  overlays.get_prediction_engine().reset();

  return true;
}

namespace {

/* Waits for a datagram on any of the sockets, for frontend events, or for
   timeout milliseconds, and tells which sockets are readable. All sockets
   signal one event object, which WSAEventSelect() also uses to make them
   non-blocking. */
class Waiter {
public:
  Waiter() : network_event( WSACreateEvent() ), associated(), consecutive_polls( 0 )
  {
    if ( network_event == WSA_INVALID_EVENT ) {
      throw Network::NetworkException( "WSACreateEvent", WSAGetLastError() );
    }
  }
  ~Waiter() { WSACloseEvent( network_event ); }

  /* Returns, for each socket, whether it has a datagram to read. */
  std::vector< bool > wait( const std::vector< SOCKET > &socks, HANDLE frontend_event, int timeout )
  {
    /* Sockets the network layer created since the last call (it opens a new
       one when it changes port). Closed sockets drop out by themselves. */
    std::vector< SOCKET > still_open;
    for ( std::vector< SOCKET >::const_iterator it = socks.begin(); it != socks.end(); it++ ) {
      bool known = false;
      for ( size_t i = 0; i < associated.size(); i++ ) {
	if ( associated[ i ] == *it ) {
	  known = true;
	}
      }
      if ( !known && WSAEventSelect( *it, network_event, FD_READ ) != 0 ) {
	throw Network::NetworkException( "WSAEventSelect", WSAGetLastError() );
      }
      still_open.push_back( *it );
    }
    associated = still_open;

    /* As Select did: a run of zero timeouts means something is wrong, so
       slow down rather than spin. */
    if ( timeout == 0 && ++consecutive_polls >= MAX_POLLS ) {
      timeout = 1;
    } else if ( timeout != 0 ) {
      consecutive_polls = 0;
    }

    /* Data that is already there doesn't always signal the event again, so
       look before waiting. */
    std::vector< bool > ready = readable( socks );
    bool any = false;
    for ( size_t i = 0; i < ready.size(); i++ ) {
      any = any || ready[ i ];
    }
    if ( !any ) {
      HANDLE handles[ 2 ] = { network_event, frontend_event };
      DWORD ms = timeout < 0 ? INFINITE : static_cast<DWORD>( timeout );
      DWORD r = WaitForMultipleObjects( 2, handles, FALSE, ms );
      if ( r == WAIT_FAILED ) {
	throw Network::NetworkException( "WaitForMultipleObjects", static_cast<int>( GetLastError() ) );
      }
      WSAResetEvent( network_event );
      ready = readable( socks );
    }

    freeze_timestamp();

    return ready;
  }

private:
  static std::vector< bool > readable( const std::vector< SOCKET > &socks )
  {
    std::vector< bool > ready( socks.size(), false );
    if ( socks.empty() ) {
      return ready;
    }
    std::vector< WSAPOLLFD > fds( socks.size() );
    for ( size_t i = 0; i < socks.size(); i++ ) {
      fds[ i ].fd = socks[ i ];
      fds[ i ].events = POLLRDNORM;
      fds[ i ].revents = 0;
    }
    if ( WSAPoll( &fds[ 0 ], static_cast<ULONG>( fds.size() ), 0 ) == SOCKET_ERROR ) {
      throw Network::NetworkException( "WSAPoll", WSAGetLastError() );
    }
    for ( size_t i = 0; i < socks.size(); i++ ) {
      /* An error on the socket also counts: recvfrom() then reports it. */
      ready[ i ] = ( fds[ i ].revents & ( POLLRDNORM | POLLERR | POLLHUP ) ) != 0;
    }
    return ready;
  }

  static const int MAX_POLLS = 10;

  WSAEVENT network_event;
  std::vector< SOCKET > associated;
  int consecutive_polls;

  Waiter( const Waiter & );
  Waiter &operator=( const Waiter & );
};

}

bool STMClient::main( void )
{
  /* initialize structures */
  main_init();

  /* prepare to wait for events */
  Waiter waiter;

  while ( 1 ) {
    try {
      output_new_frame();

      int wait_time = std::min( network->wait_time(), overlays.wait_time() );

      /* Handle startup "Connecting..." message */
      if ( still_connecting() ) {
	wait_time = std::min( 250, wait_time );
      }

      /* wait for events */
      /* network->fds() can in theory change over time */
      std::vector< SOCKET > fd_list( network->fds() );
      std::vector< bool > ready = waiter.wait( fd_list, frontend.event(), wait_time );

      bool network_ready_to_read = false;

      for ( size_t i = 0; i < ready.size(); i++ ) {
	if ( ready[ i ] ) {
	  /* packet received from the network */
	  /* we only read one socket each run */
	  network_ready_to_read = true;
	}
      }

      if ( network_ready_to_read ) {
	process_network_input();
      }

      std::deque< STMFrontend::Event > events( frontend.take_events() );
      bool quit_now = false;
      for ( std::deque< STMFrontend::Event >::const_iterator it = events.begin();
	    it != events.end();
	    it++ ) {
	if ( it->type == STMFrontend::INPUT ) {
	  if ( !process_user_input( it->bytes ) ) { /* input from the user needs to be fed to the network */
	    if ( !network->has_remote_addr() ) {
	      quit_now = true;
	    } else if ( !network->shutdown_in_progress() ) {
	      overlays.get_notification_engine().set_notification_string( u32string( U"Exiting..." ), true );
	      network->start_shutdown();
	    }
	  }
	} else if ( it->type == STMFrontend::RESIZE ) {
	  if ( !process_resize( it->width, it->height ) ) {
	    return false;
	  }
	} else if ( it->type == STMFrontend::SHUTDOWN ) {
	  /* shutdown request, as SIGTERM was */
	  if ( !network->has_remote_addr() ) {
	    quit_now = true;
	  } else if ( !network->shutdown_in_progress() ) {
	    overlays.get_notification_engine().set_notification_string( u32string( U"Exiting..." ), true );
	    network->start_shutdown();
	  }
	}
      }
      if ( quit_now ) {
	break;
      }

      /* quit if our shutdown has been acknowledged */
      if ( network->shutdown_in_progress() && network->shutdown_acknowledged() ) {
	clean_shutdown = true;
	break;
      }

      /* quit after shutdown acknowledgement timeout */
      if ( network->shutdown_in_progress() && network->shutdown_ack_timed_out() ) {
	break;
      }

      /* quit if we received and acknowledged a shutdown request */
      if ( network->counterparty_shutdown_ack_sent() ) {
	clean_shutdown = true;
	break;
      }

      /* write diagnostic message if can't reach server */
      if ( still_connecting()
	   && (!network->shutdown_in_progress())
	   && (timestamp() - network->get_latest_remote_state().timestamp > 250) ) {
	if ( timestamp() - network->get_latest_remote_state().timestamp > 15000 ) {
	  if ( !network->shutdown_in_progress() ) {
	    overlays.get_notification_engine().set_notification_string( u32string( U"Timed out waiting for server..." ), true );
	    network->start_shutdown();
	  }
	} else {
	  overlays.get_notification_engine().set_notification_string( connecting_notification );
	}
      } else if ( (network->get_remote_state_num() != 0)
		  && (overlays.get_notification_engine().get_notification_string()
		      == connecting_notification) ) {
	overlays.get_notification_engine().set_notification_string( U"" );
      }

      network->tick();

      string & send_error = network->get_send_error();
      if ( !send_error.empty() ) {
        overlays.get_notification_engine().set_network_error( send_error );
	send_error.clear();
      } else {
        overlays.get_notification_engine().clear_network_error();
      }
    } catch ( const Network::NetworkException &e ) {
      if ( !network->shutdown_in_progress() ) {
        overlays.get_notification_engine().set_network_error( e.what() );
      }

      Sleep( 200 ); /* 0.2 sec */
      freeze_timestamp();
    } catch ( const Crypto::CryptoException &e ) {
      if ( e.fatal ) {
        throw;
      } else {
        overlays.get_notification_engine().set_notification_string( u32( std::string( "Crypto exception: " ) + e.what() ) );
      }
    }
  }
  return clean_shutdown;
}
