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
	if (currentBuffer == nullptr)
		return {0, nullptr};
	return {currentBuffer->length(), currentBuffer->data()};
}

void DynamicBuffer::advanceBuffer(size_t count) {
	auto buffer = currentBuffer;
	while (count > 0 && buffer != nullptr) {
		count = buffer->advance(count);
		if (count > 0)
			buffer = buffer->nextBuffer();
	}
	while (buffer != nullptr && buffer->length() == 0)
		buffer = buffer->nextBuffer();
	currentBuffer = buffer;
}

void DynamicBuffer::addBuffer(std::shared_ptr<Buffer> buffer) {
	if (buffer->length() == 0)
		return;
	if (currentBuffer == nullptr) {
		currentBuffer = std::move(buffer);
		lastBuffer = currentBuffer;
	} else {
		auto lastBufferLocked = this->lastBuffer.lock();
		assert(lastBufferLocked != nullptr);
		assert(lastBufferLocked->nextBuffer() == nullptr);
		lastBufferLocked->nextBuffer(buffer);
		lastBuffer = buffer;
	}
}

void DynamicBuffer::addBuffer(DynamicBuffer buffer) {
	while (buffer.currentBuffer != nullptr) {
		addBuffer(buffer.currentBuffer);
		buffer.currentBuffer = buffer.currentBuffer->nextBuffer();
	}
}

bool DynamicBuffer::peekNext(void *dst, size_t length) {
	// Determine if we have enough data to do the transfer
	size_t availableData = 0;
	for (auto it = currentBuffer; it != nullptr && availableData < length; it = it->nextBuffer()) {
		availableData += it->length();
	}
	if (availableData < length)
		return false;
	
	// Transfer
	size_t transferred = 0;
	for (auto it = currentBuffer; it != nullptr && transferred < length; it = it->nextBuffer()) {
		availableData += it->length();
		size_t transferRemaining = length - transferred;
		auto [chunkRemaining, chunkData] = getNextChunk();
		auto chunkTransfer = std::min(transferRemaining, chunkRemaining);
		memcpy(static_cast<uint8_t*>(dst)+transferred, chunkData, chunkTransfer);
		transferred += chunkTransfer;
	}
	assert(transferred == length);
	return true;
}

bool DynamicBuffer::getNext(void *dst, size_t length) {
	// Determine if we have enough data to do the transfer
	size_t availableData = 0;
	for (auto it = currentBuffer; it != nullptr && availableData < length; it = it->nextBuffer()) {
		availableData += it->length();
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
