/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * file_sink_compressor.h - File Sink Compressor
 */
#ifndef __CAM_FILE_SINK_COMPRESSOR_H__
#define __CAM_FILE_SINK_COMPRESSOR_H__

#include <map>
#include <string>

#include <libcamera/buffer.h>

#include "frame_sink.h"

class Encoder;

class FileSinkCompressor : public FrameSink
{
public:
	FileSinkCompressor(const std::string &pattern = "frame-#.jpg");
	~FileSinkCompressor();

	int configure(const libcamera::CameraConfiguration &config) override;

	bool consumeBuffer(const libcamera::Stream *stream,
			   libcamera::FrameBuffer *buffer) override;

private:
	std::map<const libcamera::Stream *, std::string> streamNames_;
	std::string pattern_;

	Encoder *compressor_;
};

#endif /* __CAM_FILE_SINK_COMPRESSOR_H__ */
