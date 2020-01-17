#include <Selector.h>

#include <unistd.h>
#include <string>
#include <algorithm>
#include <cstring>
#include <utility>

Buffer::Buffer(const std::string& str) :
		mData(new BufferByte[str.length()]), mOffset(0), mLength(str.length()) {
	memcpy(this->mData.get(), reinterpret_cast<const BufferByte*>(&str[0]), str.length());
}

Buffer::Buffer(const void *data, size_t length) :
		mData(new BufferByte[length]), mOffset(0), mLength(length) {
	memcpy(this->mData.get(), data, length);
}

Buffer::Buffer(std::unique_ptr<BufferByte[]> data, size_t length) :
		mData(std::move(data)), mOffset(0), mLength(length) {
}

void DynamicBuffer::advanceBuffer(size_t count) {
	while (count > 0 && !buffers.empty()) {
		if (count == 0)
			break;
		auto & front = buffers.front();
		assert(front->length() > 0); // Should have been cleaned up
		count = front->advance(count);
		if (count > 0)
			buffers.pop_front();
	}
	while (!buffers.empty() && buffers.front()->length() == 0)
		buffers.pop_front();
}

void DynamicBuffer::addBuffer(const std::shared_ptr<Buffer>& buffer) {
	if (buffer->length() > 0)
		buffers.emplace_back(buffer);
}

void DynamicBuffer::addBuffer(DynamicBuffer& buffer) {
	while (!buffer.buffers.empty()) {
		assert(buffer.buffers.front()->length() > 0); // Should have been cleaned up
		buffers.emplace_back(buffer.buffers.front());
		buffer.buffers.pop_front();
	}
}

bool DynamicBuffer::peekNext(void *dst, size_t length) {
	if (DynamicBuffer::length() < length)
		return false;
	
	// Transfer
	size_t transferred = 0;
	for (const auto & buf : buffers) {
		size_t transferRemaining = length - transferred;
		const auto chunkRemaining = buf->length();
		const auto chunkData = buf->data();
		const auto chunkTransfer = std::min(transferRemaining, chunkRemaining);
		memcpy(static_cast<BufferByte*>(dst)+transferred, chunkData, chunkTransfer);
		transferred += chunkTransfer;
		if (transferred >= length)
			break;
	}
	assert(transferred == length);
	return true;
}

bool DynamicBuffer::getNext(void *dst, size_t length) {
	if (DynamicBuffer::length() < length)
		return false;
	
	// Transfer
	size_t transferred = 0;
	while (transferred < length) {
		size_t transferRemaining = length - transferred;
		auto buffer = getNextBuffer();
		auto chunkTransfer = std::min(transferRemaining, buffer->length());
		memcpy(static_cast<BufferByte*>(dst)+transferred, buffer->data(), chunkTransfer);
		transferred += chunkTransfer;
		advanceBuffer(chunkTransfer);
	}
	assert(transferred == length);
	return true;
}

Buffer DynamicBuffer::getNext(size_t length) {
	assert(DynamicBuffer::length() >= length);
	auto data = std::unique_ptr<BufferByte[]>(new BufferByte[length]);
	// Transfer
	size_t transferred = 0;
	while (transferred < length) {
		size_t transferRemaining = length - transferred;
		auto buffer = getNextBuffer();
		auto chunkTransfer = std::min(transferRemaining, buffer->length());
		memcpy(data.get()+transferred, buffer->data(), chunkTransfer);
		transferred += chunkTransfer;
		advanceBuffer(chunkTransfer);
	}
	assert(transferred == length);
	return Buffer(std::move(data), length);
}
