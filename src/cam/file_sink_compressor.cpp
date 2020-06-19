/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * file_sink_compressor.cpp - File Sink using libjpeg
 */

#include "../android/jpeg/encoder_libjpeg.h"
#include "file_sink_compressor.h"

#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <libcamera/camera.h>

using namespace libcamera;

FileSinkCompressor::FileSinkCompressor(const std::string &pattern)
	: pattern_(pattern)
{
	std::cerr << "Creating a FileSinkCompressor with pattern: " << pattern << std::endl;

	compressor_ = new EncoderLibJpeg();
}

FileSinkCompressor::~FileSinkCompressor()
{
	delete compressor_;
}

int FileSinkCompressor::configure(const libcamera::CameraConfiguration &config)
{
	int ret = FrameSink::configure(config);
	if (ret < 0)
		return ret;

	/* \todo:
	 * Support more streams, dynamically createing compressors as required.
	 */
	if (config.size() > 1) {
		std::cerr << "Unsupported streams" << std::endl;
		return -1;
	}

	streamNames_.clear();
	for (unsigned int index = 0; index < config.size(); ++index) {
		const StreamConfiguration &cfg = config.at(index);
		streamNames_[cfg.stream()] = "stream" + std::to_string(index);
	}

	/* Configure against the first stream only at the moment. */
	ret = compressor_->configure(config.at(0));
	if (ret)
		std::cerr << "Failed to configure JPEG compressor" << std::endl;

	return ret;
}

bool FileSinkCompressor::consumeBuffer(const Stream *stream, FrameBuffer *buffer)
{
	std::string filename;
	size_t pos;
	int fd, ret = 0;

	filename = pattern_;
	pos = filename.find_first_of('#');
	if (pos != std::string::npos) {
		std::stringstream ss;
		ss << streamNames_[stream] << "-" << std::setw(6)
		   << std::setfill('0') << buffer->metadata().sequence;
		filename.replace(pos, 1, ss.str());
	}

	size_t maxJpegSize = 13 << 20; /* 13631488 from USB HAL */
	uint8_t *jpeg = static_cast<uint8_t *>(malloc(maxJpegSize));

	/* Try to compress first */
	int size = compressor_->encode(buffer, { jpeg, maxJpegSize });
	if (size < 0) {
		std::cerr << "Failed to compress frame: " << filename << std::endl;
		return ret;
	}

	fd = open(filename.c_str(), O_CREAT | O_WRONLY | (pos == std::string::npos ? O_APPEND : O_TRUNC),
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd == -1) {
		ret = errno;
		std::cerr << "Failed to open file: " << filename
			  << ": " << strerror(ret) << std::endl;
		return true;
	}

	/* Save the compressed image. */
	ret = ::write(fd, jpeg, size);
	if (ret < 0) {
		ret = -errno;
		std::cerr << "write error: " << strerror(-ret)
			  << std::endl;
	} else if (ret != size) {
		std::cerr << "write error: only " << ret
			  << " bytes written instead of "
			  << size << std::endl;
	}

	close(fd);

	free(jpeg);

	return true;
}
