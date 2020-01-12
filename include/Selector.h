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

class Buffer {
	uint8_t * mData   = nullptr;
	size_t    mLength = 0;
	size_t    mOffset = 0;
	std::shared_ptr<Buffer> mNextBuffer = nullptr;
	
	public:
	Buffer(const uint8_t * data, size_t offset, size_t length);
	~Buffer();
	
	[[nodiscard]] inline const uint8_t * data() const { assert(mOffset <= mLength); return mData + mOffset; }
	[[nodiscard]] inline size_t length() const { assert(mOffset <= mLength); return mLength - mOffset; }
	[[nodiscard]] inline std::shared_ptr<Buffer> nextBuffer() { return mNextBuffer; }
	
	void advance(size_t offset) {
		auto newOffset = mOffset + offset;
		if (newOffset > mLength)
			newOffset = mLength;
		mOffset = newOffset;
	}
};

class DynamicBuffer {
	std::shared_ptr<Buffer> currentBuffer = {};
	std::weak_ptr<Buffer>   lastBuffer    = {};
	
	public:
	DynamicBuffer() = default;
	~DynamicBuffer() = default;
	
	[[nodiscard]] std::pair<size_t, const uint8_t*> getNextChunk() const;
	void advanceBuffer(size_t count);
	void addBuffer(std::shared_ptr<Buffer>);
	[[nodiscard]] inline bool isDataReady() const { return currentBuffer != nullptr; }
	
	bool peekNext(void * dst, size_t length);
	bool getNext(void * dst, size_t length);
};

template<typename T>
class FD {
	int fd = -1;
	std::function<std::shared_ptr<Buffer>(int)> readHandler = &FD::defaultRead;
	std::function<ssize_t(int, uint8_t *, size_t)> writeHandler = &FD::defaultWrite;
	std::function<void(int)> closeHandler = &FD::defaultClose;
	DynamicBuffer readBuffer;
	DynamicBuffer writeBuffer;
	std::shared_ptr<T> data;
	
	public:
	explicit FD(int fd, std::shared_ptr<T> data) : fd(fd), data(std::move(data)) {}
	FD(int fd, std::shared_ptr<T> data, std::function<std::shared_ptr<Buffer>(int)> readHandler, std::function<ssize_t(int, uint8_t *, size_t)> writeHandler, std::function<void(int)> closeHandler) : fd(fd), data(data), readHandler(std::move(readHandler)), writeHandler(std::move(writeHandler)), closeHandler(std::move(closeHandler)) {}
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
	FD<T>& operator=(FD<T> && f) {
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
	void doRead() {
		auto buffer = readHandler(fd);
		if (buffer != nullptr)
			readBuffer.addBuffer(buffer);
	}
	
	void doWrite() {
		ssize_t written;
		do {
			auto [chunkRemaining, chunkData] = writeBuffer.getNextChunk();
			assert(chunkRemaining > 0);
			written = writeHandler(fd, const_cast<uint8_t*>(chunkData), chunkRemaining);
			if (written > 0)
				writeBuffer.advanceBuffer(written);
		} while (written > 0);
	}
	
	void write(std::shared_ptr<Buffer> buffer) {
		writeBuffer.addBuffer(buffer);
	}
	
	[[nodiscard]] inline bool isReadReady() const noexcept { return readBuffer.isDataReady(); }
	[[nodiscard]] inline bool isWriteReady() const noexcept { return writeBuffer.isDataReady(); }
	[[nodiscard]] inline DynamicBuffer getReadBuffer() const noexcept { return readBuffer; }
	[[nodiscard]] inline std::shared_ptr<T> getData() const noexcept { return data; }
	
	private:
	static std::shared_ptr<Buffer> defaultRead(int fd) {
		std::array<uint8_t, 1024> readBuffer{};
		auto n = read(fd, &readBuffer, readBuffer.size());
		if (n <= 0)
			throw socket_error("connection closed");
		return std::make_unique<Buffer>(readBuffer.data(), 0, static_cast<size_t>(n));
	}
	static ssize_t defaultWrite(int fd, uint8_t * data, size_t length) {
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

template<typename T>
using SelectorReadCallback = std::function<void(int, const std::shared_ptr<T>&, DynamicBuffer)>;

template<typename T>
class Selector {
	std::vector<FD<T>> fds;
	SelectorReadCallback<T> readCallback = [](auto, auto, auto){};
	
	public:
	Selector() = default;
	~Selector() = default;
	
	inline void setReadCallback(const SelectorReadCallback<T> & callback) {
		this->readCallback = callback;
	}
	
	inline void addFD(FD<T> && fd) { fds.emplace_back(std::move(fd)); }
	/// Creates a generic FD with the default read/write/close
	inline void addFD(int fd) { fds.emplace_back(FD<T>(fd, nullptr)); }
	
	inline void writeToFD(int fd, std::shared_ptr<Buffer> buffer) {
		auto it = std::find_if(fds.begin(), fds.end(), [fd](const auto & a) { return a.getFD() == fd; });
		if (it != fds.end())
			it->write(buffer);
	}
	
	inline void removeFD(int fd) {
		auto it = std::find_if(fds.begin(), fds.end(), [fd](const auto & a) { return a.getFD() == fd; });
		if (it != fds.end())
			fds.erase(it);
	}
	
	void clearFDs() {
		fds.clear();
	}
	
	void selectLoop() {
		// Ignore any signals that happen to occur outside of the loop
		struct sigaction s = {};
		s.sa_handler = &Selector::ignoreSignal;
		sigemptyset(&s.sa_mask);
		sigaction(SIGINT, &s, NULL);
		sigaction(SIGTERM, &s, NULL);
		
		// Formally block the signals from occurring, except in pselect
		sigset_t sigset, prevsigset;
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGINT);
		sigaddset(&sigset, SIGTERM);
		sigprocmask(SIG_BLOCK, &sigset, &prevsigset);
		
		auto fdcollection = getFDCollection();
		auto possibleFDs = std::vector<int>{};
		reinitializePossibleFDs(possibleFDs);
		while (pselect(fdcollection.maxFD, &fdcollection.read, &fdcollection.write, &fdcollection.except, nullptr, &prevsigset) > 0) {
			for (auto & fd : possibleFDs) {
				if (FD_ISSET(fd, &fdcollection.read)) {
					auto fdIt = std::find_if(fds.begin(), fds.end(), [fd](const auto & a) { return a.getFD() == fd; });
					if (fdIt != fds.end()) {
						try {
							fdIt->doRead();
							// Unfortunately need to find it again, due to the possibility that fdIt changed
							fdIt = std::find_if(fds.begin(), fds.end(), [fd](const auto & a) { return a.getFD() == fd; });
							if (fdIt != fds.end() && fdIt->isReadReady())
								readCallback(fd, fdIt->getData(), fdIt->getReadBuffer());
						} catch (const socket_error & se) {
							removeFD(fd);
						}
					}
				} else if (FD_ISSET(fd, &fdcollection.write)) {
					auto fdObj = std::find_if(fds.begin(), fds.end(), [fd](const auto & a) { return a.getFD() == fd; });
					if (fdObj != fds.end())
						fdObj->doWrite();
				} else if (FD_ISSET(fd, &fdcollection.except)) {
					removeFD(fd);
				}
			}
			fdcollection = getFDCollection();
			reinitializePossibleFDs(possibleFDs);
		}
		
		if (errno == EINTR) {
			fprintf(stdout, "\nReceived signal requesting shutdown. Shutting down...\n");
		} else {
			fprintf(stdout, "\nUnknown socket error: %s\n", strerror(errno));
		}
	}
	
	private:
	fd_collection getFDCollection() {
		fd_collection collection{};
		FD_ZERO(&collection.read);
		FD_ZERO(&collection.write);
		FD_ZERO(&collection.except);
		
		int maxFD = 0;
		for (const auto & fd : fds) {
			int fdNum = fd.getFD();
			FD_SET(fdNum, &collection.read);
			FD_SET(fdNum, &collection.except);
			if (fd.isWriteReady())
				FD_SET(fdNum, &collection.write);
			if (fdNum > maxFD)
				maxFD = fdNum;
		}
		collection.maxFD = maxFD+1;
		return collection;
	}
	
	void reinitializePossibleFDs(std::vector<int> & possibleFDs) {
		possibleFDs.clear();
		for (const auto & fd : fds) {
			possibleFDs.push_back(fd.getFD());
		}
	}
	
	static void ignoreSignal(int signal) {
		(void) signal;
	}
	
};
