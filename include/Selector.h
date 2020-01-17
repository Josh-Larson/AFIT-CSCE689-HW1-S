#pragma once

#include "exceptions.h"

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <csignal>
#include <unistd.h>
#include <string>
#include <algorithm>
#include <cstring>
#include <utility>
#include <memory>
#include <functional>
#include <cassert>
#include <atomic>
#include <optional>
#include <vector>
#include <list>

using BufferByte = int8_t;

class Buffer {
	std::unique_ptr<BufferByte[]> mData = nullptr;
	size_t mLength = 0;
	size_t mOffset = 0;
	
	public:
	explicit Buffer(const std::string& str);
	Buffer(const void *data, size_t length);
	Buffer(std::unique_ptr<BufferByte[]> data, size_t length);
	~Buffer() = default;
	
	[[nodiscard]] inline const BufferByte * data() const { assert(mOffset <= mLength); return mData.get() + mOffset; }
	[[nodiscard]] inline size_t length() const { assert(mOffset <= mLength); return mLength - mOffset; }
	
	ssize_t advance(size_t offset) {
		const auto currentLength = mLength;
		auto newOffset = mOffset + offset;
		if (newOffset > currentLength) {
			auto remaining = newOffset - currentLength;
			mOffset = currentLength;
			return remaining;
		}
		mOffset = newOffset;
		return 0;
	}
	
	[[nodiscard]] inline BufferByte get(size_t i) const noexcept {
		assert(i >= 0);
		assert(mOffset + i < mLength);
		return mData.get()[mOffset+i];
	}
	
	inline BufferByte operator[](size_t i) const noexcept {
		return get(i);
	}
};

class DynamicBuffer {
	std::list<std::shared_ptr<Buffer>> buffers{};
	
	public:
	DynamicBuffer() = default;
	~DynamicBuffer() = default;
	
	std::shared_ptr<Buffer> getNextBuffer() const { assert(!buffers.empty()); return buffers.front(); }
	void advanceBuffer(size_t count);
	void addBuffer(const std::shared_ptr<Buffer>&);
	void addBuffer(DynamicBuffer&);
	[[nodiscard]] inline bool isDataReady() const noexcept { return !buffers.empty(); }
	
	[[nodiscard]] size_t length() const noexcept {
		size_t len = 0;
		for (const auto& buf : buffers) {
			assert(buf->length() > 0); // Should have been cleaned up
			len += buf->length();
		}
		return len;
	}
	
	BufferByte operator[](size_t i) const noexcept {
		assert(!buffers.empty());
		
		size_t len = 0;
		for (const auto& buf : buffers) {
			assert(buf->length() > 0); // Should have been cleaned up
			auto prevLen = len;
			len += buf->length();
			if (len > i)
				return buf->get(i - prevLen);
		}
		assert(false); // Ran off end of buffer
		return 0xAF;   // Odd return value that should indicate we screwed up
	}
	
	bool peekNext(void * dst, size_t length);
	bool getNext(void * dst, size_t length);
	Buffer getNext(size_t length);
};

template<typename T>
class FD {
	int fd = -1;
	std::function<std::shared_ptr<Buffer>(int)> readHandler = &FD::defaultRead;
	std::function<ssize_t(int, const BufferByte *, size_t)> writeHandler = &FD::defaultWrite;
	std::function<void(int)> closeHandler = &FD::defaultClose;
	DynamicBuffer readBuffer  = {};
	DynamicBuffer writeBuffer = {};
	std::shared_ptr<T> data   = nullptr;
	
	public:
	explicit FD(int fd, std::shared_ptr<T> data) : fd(fd), data(std::move(data)) {}
	FD(int fd, std::shared_ptr<T> data, std::optional<std::function<std::shared_ptr<Buffer>(int)>> readHandler, std::optional<std::function<ssize_t(int, const BufferByte *, size_t)>> writeHandler, std::optional<std::function<void(int)>> closeHandler) :
			fd(fd), data(data),
			readHandler(readHandler.has_value() ? std::move(*readHandler) : &FD::defaultRead),
			writeHandler(writeHandler.has_value() ? std::move(*writeHandler) : &FD::defaultWrite),
			closeHandler(closeHandler.has_value() ? std::move(*closeHandler) : &FD::defaultClose) {}
	FD(const FD<T> &) = delete; // Can't copy a file descriptor
	FD<T>& operator=(const FD<T> &) = delete; // Can't copy a file descriptor
	FD(FD<T> && f) noexcept : fd(f.fd),
					 readHandler(std::move(f.readHandler)),
					 writeHandler(std::move(f.writeHandler)),
					 closeHandler(std::move(f.closeHandler)),
					 readBuffer(std::move(f.readBuffer)),
					 writeBuffer(std::move(f.writeBuffer)),
					 data(std::move(f.data)) {
		f.fd = -1;
	}
	FD<T>& operator=(FD<T> && f) noexcept {
		if (fd >= 0)
			closeHandler(fd);
		fd = f.fd;
		readHandler = std::move(f.readHandler);
		writeHandler = std::move(f.writeHandler);
		closeHandler = std::move(f.closeHandler);
		readBuffer = std::move(f.readBuffer);
		writeBuffer = std::move(f.writeBuffer);
		data = std::move(f.data);
		f.fd = -1;
	}
	~FD() {
		if (fd >= 0)
			closeHandler(fd);
		fd = -1;
	}
	
	[[nodiscard]] inline int getFD() const noexcept { return fd; }
	std::shared_ptr<Buffer> doRead() {
		return readHandler(fd);
	}
	
	void doWrite() {
		ssize_t written;
		do {
			if (!writeBuffer.isDataReady())
				return;
			auto buffer = writeBuffer.getNextBuffer();
			written = writeHandler(fd, buffer->data(), buffer->length());
			if (written > 0)
				writeBuffer.advanceBuffer(written);
			else if (written < 0 && !(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
				throw socket_error(std::string("failed to write to socket: ") + strerror(errno));
			else if (written == 0)
				break;
		} while (written > 0);
	}
	
	[[nodiscard]] inline DynamicBuffer& getReadBuffer() noexcept { return *(&readBuffer); }
	[[nodiscard]] inline DynamicBuffer& getWriteBuffer() noexcept { return *(&writeBuffer); }
	[[nodiscard]] inline std::shared_ptr<T> getData() const noexcept { return data; }
	
	private:
	static std::shared_ptr<Buffer> defaultRead(int fd) {
		std::array<BufferByte, 1024> readBuffer{};
		auto n = read(fd, readBuffer.data(), readBuffer.size());
		if (n <= 0)
			throw socket_error("connection closed");
		return std::make_unique<Buffer>(readBuffer.data(), static_cast<size_t>(n));
	}
	
	static ssize_t defaultWrite(int fd, const BufferByte * data, size_t length) {
		return ::write(fd, data, length);
	}
	
	static void defaultClose(int fd) {
		close(fd);
	}
	
};

struct fd_collection {
	int maxFD;
	fd_set read;
	fd_set write;
	fd_set except;
};

enum class SelectLoopTermination {
	SUCCESS,
	INTERRUPTED,
	SOCKET_ERROR
};

template<typename T>
using SelectorReadCallback = std::function<void(int, const std::shared_ptr<T>&, DynamicBuffer&)>;

template<typename T>
class Selector {
	using FDPTR = std::shared_ptr<FD<T>>;
	std::vector<FDPTR> fds;
	SelectorReadCallback<T> readCallback = [](auto, auto, auto){};
	std::atomic<bool> running = true;
	
	public:
	Selector() = default;
	~Selector() = default;
	
	inline void setReadCallback(const SelectorReadCallback<T> & callback) {
		this->readCallback = callback;
	}
	
	inline void addFD(FD<T> && fd) { fds.emplace_back(std::make_shared<FD<T>>(std::move(fd))); }
	/// Creates a generic FD with the default read/write/close
	inline void addFD(int fd) { fds.emplace_back(std::make_shared<FD<T>>(fd, nullptr)); }
	
	void writeToFD(int fd, std::shared_ptr<Buffer> buffer) {
		runIfFDFound(fd, [&buffer](FDPTR it) {
			it->getWriteBuffer().addBuffer(buffer);
		});
	}
	
	void writeToFD(int fd, DynamicBuffer & buffer) {
		runIfFDFound(fd, [&buffer](FDPTR it) {
			it->getWriteBuffer().addBuffer(buffer);
		});
	}
	
	void removeFD(int fd) {
		const auto it = std::find_if(std::cbegin(fds), std::cend(fds), [fd](const auto & a) { return a->getFD() == fd; });
		if (it != std::cend(fds)) {
			fprintf(stdout, "Closing connection to FD %d\n", (*it)->getFD());
			fds.erase(it);
		}
	}
	
	void clearFDs() {
		fds.clear();
	}
	
	void start() {
		running = true;
	}
	
	void stop() {
		running = false;
	}
	
	SelectLoopTermination singleSelectLoop() {
		auto prevsigset = initializeSignalBlocks();
		auto fdcollection = getFDCollection();
		auto possibleFDs = std::vector<int>{};
		reinitializePossibleFDs(possibleFDs);
		
		int ret = pselect(fdcollection.maxFD, &fdcollection.read, &fdcollection.write, &fdcollection.except, nullptr, &prevsigset);
		if (ret > 0) {
			for (auto & fd : possibleFDs) {
				handleFileDescriptorReady(fd, fdcollection);
			}
			fdcollection = getFDCollection();
			reinitializePossibleFDs(possibleFDs);
		}
		
		if (ret >= 0)
			return SelectLoopTermination::SUCCESS;
		if (errno == EINTR)
			return SelectLoopTermination::INTERRUPTED;
		return SelectLoopTermination::SOCKET_ERROR;
	}
	
	SelectLoopTermination selectLoop() {
		auto prevsigset = initializeSignalBlocks();
		auto fdcollection = getFDCollection();
		auto possibleFDs = std::vector<int>{};
		reinitializePossibleFDs(possibleFDs);
		
		int ret;
		while (running && (ret = pselect(fdcollection.maxFD, &fdcollection.read, &fdcollection.write, &fdcollection.except, nullptr, &prevsigset)) > 0) {
			for (auto & fd : possibleFDs) {
				handleFileDescriptorReady(fd, fdcollection);
			}
			fdcollection = getFDCollection();
			reinitializePossibleFDs(possibleFDs);
		}
		
		if (ret >= 0)
			return SelectLoopTermination::SUCCESS;
		if (errno == EINTR)
			return SelectLoopTermination::INTERRUPTED;
		return SelectLoopTermination::SOCKET_ERROR;
	}
	
	private:
	fd_collection getFDCollection() {
		fd_collection collection{};
		FD_ZERO(&collection.read);
		FD_ZERO(&collection.write);
		FD_ZERO(&collection.except);
		
		int maxFD = -1;
		for (const auto & fd : fds) {
			int fdNum = fd->getFD();
			FD_SET(fdNum, &collection.read);
			FD_SET(fdNum, &collection.except);
			if (fd->getWriteBuffer().isDataReady())
				FD_SET(fdNum, &collection.write);
			if (fdNum > maxFD)
				maxFD = fdNum;
		}
		collection.maxFD = maxFD+1;
		return collection;
	}
	
	sigset_t initializeSignalBlocks() {
		// Ignore any signals that happen to occur outside of the loop
		struct sigaction s = {};
		s.sa_handler = &Selector::ignoreSignal;
		s.sa_flags = SA_INTERRUPT;
		sigemptyset(&s.sa_mask);
		sigaction(SIGINT, &s, nullptr);
		sigaction(SIGTERM, &s, nullptr);
		
		// Formally block the signals from occurring, except in pselect
		sigset_t sigset, prevsigset;
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGINT);
		sigaddset(&sigset, SIGTERM);
		sigprocmask(SIG_BLOCK, &sigset, &prevsigset);
		return prevsigset;
	}
	
	void reinitializePossibleFDs(std::vector<int> & possibleFDs) {
		possibleFDs.clear();
		for (const auto & fd : fds) {
			possibleFDs.push_back(fd->getFD());
		}
	}
	
	void handleFileDescriptorReady(int fd, const fd_collection & fdcollection) {
		try {
			if (FD_ISSET(fd, &fdcollection.read)) {
				auto fdIt = findFD(fd);
				if (fdIt == nullptr)
					return;
				const auto buffer = fdIt->doRead();
				if (buffer != nullptr) {
					auto & readBuffer = fdIt->getReadBuffer();
					readBuffer.addBuffer(buffer);
					if (readBuffer.isDataReady())
						readCallback(fd, fdIt->getData(), readBuffer);
				}
			} else if (FD_ISSET(fd, &fdcollection.write)) {
				runIfFDFound(fd, [](FDPTR it) {
					it->doWrite();
				});
			} else if (FD_ISSET(fd, &fdcollection.except)) {
				removeFD(fd);
			}
		} catch (const socket_error & se) {
			removeFD(fd);
		}
	}
	
	inline FDPTR findFD(int fd) {
		auto it = std::find_if(std::cbegin(fds), std::cend(fds), [fd](const auto & a) { return a->getFD() == fd; });
		if (it == std::cend(fds))
			return nullptr;
		return *it;
	}
	
	inline void runIfFDFound(int fd, const std::function<void(FDPTR)> & handler) {
		auto it = std::find_if(std::cbegin(fds), std::cend(fds), [fd](const auto & a) { return a->getFD() == fd; });
		if (it != std::cend(fds))
			handler(*it);
	}
	
	static void ignoreSignal(int signal) {
		(void) signal;
	}
	
};
