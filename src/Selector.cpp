#include <Selector.h>

#include <unistd.h>
#include <string>
#include <algorithm>
#include <cstring>
#include <utility>

Buffer::Buffer(const std::string& str) :
	mData(new uint8_t[str.length()]), mOffset(0), mLength(str.length()) {
		memcpy(this->mData, reinterpret_cast<const uint8_t*>(&str[0]), str.length());
}

Buffer::Buffer(const void *data, size_t length) :
		mData(new uint8_t[length]), mOffset(0), mLength(length) {
	memcpy(this->mData, data, length);
}

Buffer::Buffer(const char *data, size_t offset, size_t length) :
		mData(new uint8_t[length]), mOffset(0), mLength(length) {
	memcpy(this->mData, data + offset, length);
}

Buffer::Buffer(const uint8_t *data, size_t offset, size_t length) :
		mData(new uint8_t[length]), mOffset(0), mLength(length) {
	memcpy(this->mData, data + offset, length);
}

Buffer::~Buffer() {
	delete [] mData;
}

std::pair<size_t, const uint8_t *> DynamicBuffer::getNextChunk() const {
	if (buffers.empty())
		return {0, nullptr};
	const auto & buf = buffers.front();
	return {buf->length(), buf->data()};
}

void DynamicBuffer::advanceBuffer(size_t count) {
	while (count > 0 && !buffers.empty()) {
		if (count == 0)
			break;
		auto & front = buffers.front();
		count = front->advance(count);
		if (count > 0)
			buffers.pop_front();
	}
	while (!buffers.empty() && buffers.front()->length() == 0)
		buffers.pop_front();
}

void DynamicBuffer::addBuffer(const std::shared_ptr<Buffer>& buffer) {
	buffers.emplace_back(buffer);
}

void DynamicBuffer::addBuffer(DynamicBuffer& buffer) {
	while (!buffer.buffers.empty()) {
		buffers.emplace_back(buffer.buffers.front());
		buffer.buffers.pop_front();
	}
}

bool DynamicBuffer::peekNext(void *dst, size_t length) {
	// Determine if we have enough data to do the transfer
	size_t availableData = 0;
	for (const auto & buf : buffers) {
		availableData += buf->length();
		if (availableData >= length)
			break;
	}
	if (availableData < length)
		return false;
	
	// Transfer
	size_t transferred = 0;
	for (const auto & buf : buffers) {
		size_t transferRemaining = length - transferred;
		const auto chunkRemaining = buf->length();
		const auto chunkData = buf->data();
		const auto chunkTransfer = std::min(transferRemaining, chunkRemaining);
		memcpy(static_cast<uint8_t*>(dst)+transferred, chunkData, chunkTransfer);
		transferred += chunkTransfer;
		if (transferred >= length)
			break;
	}
	assert(transferred == length);
	return true;
}

bool DynamicBuffer::getNext(void *dst, size_t length) {
	// Determine if we have enough data to do the transfer
	size_t availableData = 0;
	for (const auto & buf : buffers) {
		availableData += buf->length();
		if (availableData >= length)
			break;
	}
	if (availableData < length)
		return false;
	
	// Transfer
	size_t transferred = 0;
	while (transferred < length) {
		size_t transferRemaining = length - transferred;
		auto [chunkRemaining, chunkData] = getNextChunk();
		auto chunkTransfer = std::min(transferRemaining, chunkRemaining);
		memcpy(static_cast<uint8_t*>(dst)+transferred, chunkData, chunkTransfer);
		transferred += chunkTransfer;
		advanceBuffer(chunkTransfer);
	}
	assert(transferred == length);
	return true;
}
