// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Sourcey/HTTP/Form.h"
#include "Sourcey/HTTP/Client.h"
#include "Sourcey/HTTP/Packetizers.h"
#include "Sourcey/HTTP/URL.h"
#include "Sourcey/FileSystem.h"
#include "Sourcey/Crypto/Crypto.h"
#include <stdexcept>


namespace scy {
namespace http {


//
// Form Writer
//


const char* FormWriter::ENCODING_URL       = "application/x-www-form-urlencoded";
const char* FormWriter::ENCODING_MULTIPART = "multipart/form-data";

const int FILE_CHUNK_SIZE = 32384;


FormWriter* FormWriter::create(ClientConnection& conn, const std::string& encoding)
{
	FormWriter* wr = new http::FormWriter(conn, encoding);
	conn.Outgoing.attachSource(wr, true, true);
	if (encoding == http::FormWriter::ENCODING_MULTIPART &&
		conn.request().getVersion() == http::Message::HTTP_1_1)
		conn.Outgoing.attach(new http::ChunkedAdapter(&conn), 0, true);
	conn.Outgoing.lock();
	return wr;
}


FormWriter::FormWriter(ClientConnection& conn, const std::string& encoding) :
	PacketSource(this->emitter),
	_encoding(encoding),
	_connection(conn),
	_filesSize(0),
	_initial(true),
	_stopped(false)
{
}

	
FormWriter::~FormWriter()
{
	for (PartVec::iterator it = _parts.begin(); it != _parts.end(); ++it)
		delete it->part;
}


void FormWriter::addFile(const std::string& name, FilePart* part)
{
	assert(part);
	assert(_encoding == ENCODING_MULTIPART);

	Part p;
	p.part = part;
	p.name = name;
	_parts.push_back(p);

	_filesSize += part->fileSize();
}


void FormWriter::prepareSubmit()
{	
	http::Request& request = _connection.request();
	if (request.getMethod() == http::Method::Post || 
		request.getMethod() == http::Method::Put) {
		if (_encoding == ENCODING_URL) {
			request.setContentType(_encoding);
			request.setChunkedTransferEncoding(false);
			std::ostringstream ostr;
			writeUrl(ostr);
			assert(ostr.tellp() > 0);
			request.setContentLength(ostr.tellp());
		}
		else {
			if (_boundary.empty())
				_boundary = createBoundary();
			std::string ct(_encoding);
			ct.append("; boundary=\"");
			ct.append(_boundary);
			ct.append("\"");
			request.setContentType(ct);
					
			// Set the total file size for upload progress updates.
			// This is not the HTTP content length as it does not 
			// factor chunk headers.
			if (!_parts.empty()) {
				assert(_filesSize);
				_connection.OutgoingProgress.total = _filesSize;
			}
		}
		if (request.getVersion() == http::Message::HTTP_1_0) {
			request.setKeepAlive(false);
			request.setChunkedTransferEncoding(false);
		}
		else if (_encoding != ENCODING_URL) {
			request.setChunkedTransferEncoding(true);
		}
	}
	else {
		std::string uri = request.getURI();
		std::ostringstream ostr;
		writeUrl(ostr);
		uri.append("?");
		uri.append(ostr.str());
		request.setURI(uri);
	}
}


void FormWriter::runThread(void* arg)
{
	FormWriter* wr = reinterpret_cast<FormWriter*>(arg);
	traceL("FormWriter", wr) << "Running" << std::endl;
	try {		
		if (wr->encoding() == ENCODING_URL) {
			std::ostringstream ostr;
			wr->writeUrl(ostr);					
			traceL("FormWriter") << "Writing URL: " << ostr.str() << std::endl;
			wr->emit(ostr.str());
		} else
			wr->writeMultipart();
	}
	catch (std::exception& exc) {
		traceL("FormWriter", wr) << "Error: " << exc.what() << std::endl;
//#ifdef _DEBUG
//		throw exc;
//#endif
	}
	traceL("FormWriter", wr) << "Ending" << std::endl;
}


void FormWriter::start()
{
	traceL("FilePart", this) << "Starting" << std::endl;

	prepareSubmit();
	_thread.start(FormWriter::runThread, this);
}


void FormWriter::stop()
{
	traceL("FilePart", this) << "Stopping" << std::endl;

	_stopped = true;
	_thread.join();
}


void FormWriter::writeMultipart()
{
	for (NVCollection::ConstIterator it = begin(); it != end(); ++it) {
		std::ostringstream ostr;
		NVCollection header;
		std::string disp("form-data; name=\"");
		disp.append(it->first);
		disp.append("\"");
		header.set("Content-Disposition", disp);
		writePartHeader(header, ostr);
		ostr << it->second;		
		emit(ostr.str());
	}	

	for (PartVec::const_iterator pit = _parts.begin(); pit != _parts.end(); ++pit) {
		std::ostringstream ostr;
		NVCollection header(pit->part->headers());
		std::string disp("form-data; name=\"");
		disp.append(pit->name);
		disp.append("\"");
		std::string filename = pit->part->filename();
		if (!filename.empty()) {
			disp.append("; filename=\"");
			disp.append(filename);
			disp.append("\"");
		}
		header.set("Content-Disposition", disp);
		header.set("Content-Type", pit->part->contentType());
		writePartHeader(header, ostr);		
		emit(ostr.str());		
		pit->part->write(*this);
	}
	
	std::ostringstream ostr;
	writeEnd(ostr);
	emit(ostr.str());
		
	// HACK: Write chunked end code directly to the connection.
	// TODO: Send final packet flag down packet stream, or use 
	// Stream state change to trigger via chunked packetizer.
	reinterpret_cast<Connection&>(_connection).sendData("0\r\n\r\n", 5, 0);
}


void FormWriter::writeUrl(std::ostream& ostr)
{
	for (NVCollection::ConstIterator it = begin(); it != end(); ++it) {
		if (it != begin()) ostr << "&";
		ostr << URL::encode(it->first) << "=" << URL::encode(it->second);
	}
}


void FormWriter::writePartHeader(const NVCollection& header, std::ostream& ostr)
{
	if (_initial) 
		_initial = false;
	else
		ostr << "\r\n";
	ostr << "--" << _boundary << "\r\n";

	NVCollection::ConstIterator it = header.begin();
	while (it != header.end()) {
		ostr << it->first << ": " << it->second << "\r\n";
		++it;
	}
	ostr << "\r\n";
}

	
void FormWriter::writeEnd(std::ostream& ostr)
{
	ostr << "\r\n--" << _boundary << "--\r\n";
}


void FormWriter::updateProgress(int nread)
{
	_connection.OutgoingProgress.update(nread);
}


std::string FormWriter::createBoundary()
{
	return "boundary-" + util::randomString(8);
}


void FormWriter::setEncoding(const std::string& encoding)
{
	_encoding = encoding;
}


void FormWriter::setBoundary(const std::string& boundary)
{
	_boundary = boundary;
}


const std::string& FormWriter::encoding() const
{
	return _encoding;
}


const std::string& FormWriter::boundary() const
{
	return _boundary;
}


ClientConnection& FormWriter::connection()
{
	return _connection;
}


bool FormWriter::stopped() const
{
	return _stopped;
}


//
// File Part Source
//

	
FilePart::FilePart(const std::string& path) :	
	_contentType("application/octet-stream"),
	_fileSize(0)
{
	_filename = fs::filename(path);
	open(path);
}


FilePart::FilePart(const std::string& path, const std::string& contentType) :
	_contentType(contentType),
	_fileSize(0)
{
	_filename = fs::filename(path); 
	open(path);
}


FilePart::FilePart(const std::string& path, const std::string& filename, const std::string& contentType) :
	_contentType(contentType),
	_filename(filename),
	_fileSize(0)
{
	open(path);
}


FilePart::~FilePart()
{
}


void FilePart::open(const std::string& path)
{
	traceL("FilePart", this) << "Open: " << path << std::endl;

	_istr.open(path.c_str(), std::ios::in | std::ios::binary);
	if (!_istr.is_open())
		throw std::runtime_error("Cannot open file: " + path);

	// Get file size
	_istr.seekg(0, std::ios::end); 
	_fileSize = _istr.tellg();
	_istr.seekg(0, std::ios::beg); 
}


void FilePart::write(FormWriter& writer)
{
	traceL("FilePart", this) << "Write" << std::endl;

	// Send file chunks to the peer
	char buffer[FILE_CHUNK_SIZE];
	while (_istr.read(buffer, FILE_CHUNK_SIZE) && !writer.stopped()) {
		writer.emit(buffer, (size_t)_istr.gcount());
		writer.updateProgress((int)_istr.gcount());
	}

	if (_istr.eof()) {
		// Still a few bytes left to write?
		if (_istr.gcount() > 0 && !writer.stopped()) {
			writer.emit(buffer, (size_t)_istr.gcount());
			writer.updateProgress((int)_istr.gcount());
		}
	}
	else if (_istr.bad())
		throw std::runtime_error("Cannot read multipart source file: " + _filename);
}


void FilePart::write(std::ostream& ostr)
{
	traceL("FilePart", this) << "Write" << std::endl;
	
	// Send file chunks to the peer
	char buffer[FILE_CHUNK_SIZE];
	while (_istr.read(buffer, FILE_CHUNK_SIZE))
		ostr.write(buffer, (size_t)_istr.gcount());

	if (_istr.eof()) {
		// Still a few bytes left to write?
		if (_istr.gcount() > 0)
			ostr.write(buffer, (size_t)_istr.gcount());
	}
	else if (_istr.bad())
		throw std::runtime_error("Cannot read multipart source file: " + _filename);
}


const std::string& FilePart::filename() const
{
	return _filename;
}


NVCollection& FilePart::headers()
{
	return _headers;
}
	

const std::string& FilePart::contentType() const
{	
	return _contentType; 
}
	

UInt64 FilePart::fileSize() const
{	
	return _fileSize; 
}


} } // namespace scy::http





/*
void FormWriter::run()
{
	prepareSubmit(_connection.request());
	if (_encoding == ENCODING_URL) {
		std::ostringstream ostr;
		writeUrl(ostr);		
		emit(ostr.str());
	} else
		writeMultipart();
}
*/


/*
void FormWriter::onIdle()
{
	traceL("FormWriter", this) << "On idle" << std::endl;

	if (_initial)
		prepareSubmit(_connection.request());
	
	if (_encoding == ENCODING_URL) {
		std::ostringstream ostr;
		writeUrl(ostr);		
		emit(ostr.str());
		stop(); // all done
	} 
	else {
		writeMultipart();
		if (_parts.empty())
			stop(); // all done
	}
	
	_initial = false;
}
*/

//
// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
