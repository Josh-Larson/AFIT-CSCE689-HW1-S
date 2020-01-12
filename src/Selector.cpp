#include <Selector.h>

#include <unistd.h>
#include <string>
#include <algorithm>
#include <cstring>
#include <utility>

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
	assert(currentBuffer != nullptr);
	if (currentBuffer != nullptr)
		currentBuffer->advance(count);
	if (currentBuffer->length() == 0)
		currentBuffer = currentBuffer->nextBuffer();
}

void DynamicBuffer::addBuffer(std::shared_ptr<Buffer> buffer) {
	if (currentBuffer == nullptr) {
		currentBuffer = std::move(buffer);
		lastBuffer = currentBuffer;
	} else {
		auto lastBufferLocked = this->lastBuffer.lock();
		assert(lastBufferLocked != nullptr);
		assert(lastBufferLocked->nextBuffer() == nullptr);
		lastBufferLocked->nextBuffer() = buffer;
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
