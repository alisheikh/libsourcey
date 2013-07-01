//
// LibSourcey
// Copyright (C) 2005, Sourcey <http://sourcey.com>
//
// LibSourcey is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// LibSourcey is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//


#ifndef SOURCEY_Net_SSLSocket_H
#define SOURCEY_Net_SSLSocket_H


#include "Sourcey/UV/UVPP.h"
#include "Sourcey/UV/TCPBase.h"
#include "Sourcey/Net/Socket.h"
#include "Sourcey/Net/SSLContext.h"

#include "Poco/Net/SSLException.h"
#include "Poco/Net/Context.h"
#include "Poco/Net/Session.h"


namespace scy {
namespace net {


class SSLBase: public uv::TCPBase	
{
public:	
	SSLBase();
	SSLBase(Poco::Net::Context::Ptr sslContext);
	SSLBase(Poco::Net::Context::Ptr sslContext, Poco::Net::Session::Ptr session);

	//virtual void connect(const Address& peerAddress);	
		/// Initializes the socket and establishes a secure connection to 
		/// the TCP server at the given address.
		///
		/// The SSL handshake is performed the first time sendBytes(), 
		/// receiveBytes() or completeHandshake() is called.	
	
	virtual bool shutdown();

	virtual void close();
		/// Closes the socket.
		///
		/// Shuts down the connection by attempting
		/// an orderly SSL shutdown, then actually
		/// shutting down the TCP connection.
	
	virtual int send(const char* data, int len, int flags = 0);
		
	int available() const;
		/// Returns the number of bytes available from the
		/// SSL buffer for immediate reading.
	
	X509* peerCertificate() const;
		/// Returns the peer's certificate.
		
	Poco::Net::Context::Ptr context() const;
		/// Returns the SSL context used for this socket.
			
	Poco::Net::Session::Ptr currentSession();
		/// Returns the SSL session of the current connection,
		/// for reuse in a future connection (if session caching
		/// is enabled).
		///
		/// If no connection is established, returns null.
		
	void useSession(Poco::Net::Session::Ptr session);
		/// Sets the SSL session to use for the next
		/// connection. Setting a previously saved Session
		/// object is necessary to enable session caching.
		///
		/// To remove the currently set session, a null pointer
		/// can be given.
		///
		/// Must be called before connect() to be effective.
		
	bool sessionWasReused();
		/// Returns true if a reused session was negotiated during
		/// the handshake.

	virtual void onConnected(int status);

	virtual void onRead(const char* data, int len);
		/// Reads raw encrypred SSL data

	virtual void onRecv(Buffer& buf);	
		/// Reads and emits decrypted SSL data
		
protected:
	virtual void* instance() { return this; }
	virtual ~SSLBase();

protected:
	net::SSLContext _sslBuffer;
	Poco::Net::Context::Ptr _context;
	Poco::Net::Session::Ptr _session;

	friend class net::SSLContext;
};


class SSLSocket: public Socket
	/// SSLSocket is a disposable SSL socket wrapper
	/// for SSLBase which can be created on the stack.
	/// See SSLBase for implementation details.
{
public:	
	typedef net::SSLBase Base;
	typedef std::vector<SSLSocket> List;
	
	SSLSocket();
		/// Creates an unconnected SSL socket.

	SSLSocket(Poco::Net::Context::Ptr sslContext);
	SSLSocket(Poco::Net::Context::Ptr sslContext, Poco::Net::Session::Ptr session);

	SSLSocket(SSLBase* base, bool shared = false);
		/// Creates the Socket and attaches the given SocketBase.
		///
		/// The SocketBase must be a SSLBase, otherwise an
		/// exception will be thrown.

	SSLSocket(const Socket& socket);
		/// Creates the SSLSocket with the SocketBase
		/// from another socket. The SocketBase must be
		/// a SSLBase, otherwise an exception will be thrown.
	
	SSLBase& base() const;
		/// Returns the SocketBase for this socket.
};


//typedef SocketHandle<SSLSocket> SSLSocketHandle;


} } // namespace scy::uv


#endif // SOURCEY_Net_SSLSocket_H 




	
/*

typedef SSLBase SSLSocket;
class SSLBase;
class SSLSocket: public Socket
	/// SSLSocket is a disposable SSL socket wrapper
	/// for SSLBase which can be created on the stack.
	/// See SSLBase for implementation details.
{
public:	
	typedef net::SSLBase Base;
	typedef std::vector<SSLSocket> List;
	
	SSLSocket();
		/// Creates an unconnected SSL socket.

	SSLSocket(Poco::Net::Context::Ptr sslContext);
	SSLSocket(Poco::Net::Context::Ptr sslContext, Poco::Net::Session::Ptr session);

	SSLSocket(const SocketHandle& socket);
		/// Creates the SSLSocket with the SocketBase
		/// from another socket. The SocketBase must be
		/// a SSLBase, otherwise an exception will be thrown.

	SSLSocket(SSLBase* base);
	
	SSLBase* base() const;
		/// Returns the SocketBase for this socket.
};
*/