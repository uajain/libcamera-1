/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * encoder_libjpeg.cpp - JPEG encoding using libjpeg native API
 */

#include "encoder_libjpeg.h"

#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include <libcamera/camera.h>
#include <libcamera/formats.h>
#include <libcamera/pixel_format.h>

#include "libcamera/internal/formats.h"
#include "libcamera/internal/log.h"

#include "exif.h"

using namespace libcamera;

LOG_DEFINE_CATEGORY(JPEG)

/*
https://github.com/zakinster/detiq-t/blob/master/ImageIn/JpgImage.cpp
https://sourceforge.net/p/libjpeg/mailman/message/30815123/
https://raw.githubusercontent.com/libjpeg-turbo/libjpeg-turbo/master/libjpeg.txt
https://raw.githubusercontent.com/libjpeg-turbo/libjpeg-turbo/master/example.txt


# Referenced by CCA javascripts:
ANDROID_LENS_FOCUS_DISTANCE
ANDROID_SENSOR_SENSITIVITY
ANDROID_SENSOR_EXPOSURE_TIME
ANDROID_SENSOR_FRAME_DURATION
ANDROID_CONTROL_AE_ANTIBANDING_MODE
ANDROID_COLOR_CORRECTION_GAINS

# Referenced by Chrome
ANDROID_EDGE_MODE
ANDROID_NOISE_REDUCTION_MODE
ANDROID_REQUEST_PARTIAL_RESULT_COUNT
ANDROID_REQUEST_PIPELINE_MAX_DEPTH
ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS
ANDROID_SENSOR_TIMESTAMP
ANDROID_SENSOR_TEST_PATTERN_MODE
ANDROID_SENSOR_ORIENTATION
ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE
ANDROID_SYNC_MAX_LATENCY
ANDROID_JPEG_MAX_SIZE
ANDROID_JPEG_ORIENTATION
ANDROID_SCALER_AVAILABLE_FORMATS
ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS
ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP
ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM
ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS
ANDROID_SCALER_CROP_REGION
ANDROID_REQUEST_AVAILABLE_CAPABILITIES
ANDROID_CONTROL_ENABLE_ZSL
ANDROID_CONTROL_MODE
ANDROID_CONTROL_MAX_REGIONS
ANDROID_CONTROL_AVAILABLE_MODES
ANDROID_CONTROL_AE_TARGET_FPS_RANGE
ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES
ANDROID_CONTROL_AE_AVAILABLE_MODES
ANDROID_CONTROL_AWB_AVAILABLE_MODES
ANDROID_CONTROL_AE_LOCK_AVAILABLE
ANDROID_CONTROL_AE_REGIONS
ANDROID_CONTROL_AE_LOCK
ANDROID_CONTROL_AE_MODE
ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER
ANDROID_CONTROL_AE_STATE
ANDROID_CONTROL_AF_MODE
ANDROID_CONTROL_AF_AVAILABLE_MODES
ANDROID_CONTROL_AF_TRIGGER
ANDROID_CONTROL_AF_REGIONS
ANDROID_CONTROL_AF_STATE
ANDROID_CONTROL_AWB_MODE
ANDROID_CONTROL_AWB_STATE

*/

namespace {

struct JPEGPixelFormatInfo {
	J_COLOR_SPACE colorSpace;
	const PixelFormatInfo &pixelFormatInfo;
	bool nvSwap;
};

const std::map<PixelFormat, JPEGPixelFormatInfo> pixelInfo{
	{ formats::R8, { JCS_GRAYSCALE, PixelFormatInfo::info(formats::R8), false } },

	{ formats::RGB888, { JCS_EXT_BGR, PixelFormatInfo::info(formats::RGB888), false } },
	{ formats::BGR888, { JCS_EXT_RGB, PixelFormatInfo::info(formats::BGR888), false } },

	/* YUV packed formats. */
	{ formats::UYVY, { JCS_YCbCr, PixelFormatInfo::info(formats::UYVY), false } },
	{ formats::VYUY, { JCS_YCbCr, PixelFormatInfo::info(formats::VYUY), false } },
	{ formats::YUYV, { JCS_YCbCr, PixelFormatInfo::info(formats::YUYV), false } },
	{ formats::YVYU, { JCS_YCbCr, PixelFormatInfo::info(formats::YVYU), false } },

	/* YUY planar formats. */
	{ formats::NV12, { JCS_YCbCr, PixelFormatInfo::info(formats::NV12), false } },
	{ formats::NV21, { JCS_YCbCr, PixelFormatInfo::info(formats::NV21), true } },
	{ formats::NV16, { JCS_YCbCr, PixelFormatInfo::info(formats::NV16), false } },
	{ formats::NV61, { JCS_YCbCr, PixelFormatInfo::info(formats::NV61), true } },
	{ formats::NV24, { JCS_YCbCr, PixelFormatInfo::info(formats::NV24), false } },
	{ formats::NV42, { JCS_YCbCr, PixelFormatInfo::info(formats::NV42), true } },
};

const struct JPEGPixelFormatInfo &findPixelInfo(const PixelFormat &format)
{
	static const struct JPEGPixelFormatInfo invalidPixelFormat {
		JCS_UNKNOWN, PixelFormatInfo(), false
	};

	const auto iter = pixelInfo.find(format);
	if (iter == pixelInfo.end()) {
		LOG(JPEG, Error) << "Unsupported pixel format for JPEG encoder: "
				 << format.toString();
		return invalidPixelFormat;
	}

	return iter->second;
}

} /* namespace */

EncoderLibJpeg::EncoderLibJpeg()
	: quality_(95)
{
	/* \todo Expand error handling coverage with a custom handler. */
	compress_.err = jpeg_std_error(&jerr_);

	jpeg_create_compress(&compress_);
}

EncoderLibJpeg::~EncoderLibJpeg()
{
	jpeg_destroy_compress(&compress_);
}

int EncoderLibJpeg::configure(const StreamConfiguration &cfg)
{
	{
		LOG(JPEG, Warning) << "Configuring pixelformat as : "
				   << cfg.pixelFormat.toString();
		LOG(JPEG, Warning) << "  : " << cfg.toString();

		std::vector<PixelFormat> formats = cfg.formats().pixelformats();
		LOG(JPEG, Warning) << "StreamConfiguration supports " << formats.size() << " formats:";
		for (const PixelFormat &format : formats)
			LOG(JPEG, Warning) << " - " << format.toString();
	}

	const struct JPEGPixelFormatInfo info = findPixelInfo(cfg.pixelFormat);
	if (info.colorSpace == JCS_UNKNOWN)
		return -ENOTSUP;

	compress_.image_width = cfg.size.width;
	compress_.image_height = cfg.size.height;
	compress_.in_color_space = info.colorSpace;

	compress_.input_components = info.colorSpace == JCS_GRAYSCALE ? 1 : 3;

	jpeg_set_defaults(&compress_);
	jpeg_set_quality(&compress_, quality_, TRUE);

	pixelFormatInfo_ = &info.pixelFormatInfo;

	nv_ = pixelFormatInfo_->numPlanes() == 2;
	nvSwap_ = info.nvSwap;

	return 0;
}

void EncoderLibJpeg::compressRGB(const libcamera::MappedBuffer *frame)
{
	unsigned char *src = static_cast<unsigned char *>(frame->maps()[0].data());
	/* \todo Stride information should come from buffer configuration. */
	unsigned int stride = pixelFormatInfo_->stride(compress_.image_width, 0);

	JSAMPROW row_pointer[1];

	while (compress_.next_scanline < compress_.image_height) {
		row_pointer[0] = &src[compress_.next_scanline * stride];
		jpeg_write_scanlines(&compress_, row_pointer, 1);
	}
}

/*
 * A very dull implementation to compress YUYV.
 * To be converted to a generic algorithm akin to NV12.
 * If it can be shared with NV12 great, but we might be able to further
 * optimisze the NV layouts by only depacking the CrCb pixels.
 */
void EncoderLibJpeg::compressYUV(const libcamera::MappedBuffer *frame)
{
	std::vector<uint8_t> tmprowbuf(compress_.image_width * 3);
	unsigned char *input = static_cast<unsigned char *>(frame->maps()[0].data());
	unsigned int stride = pixelFormatInfo_->stride(compress_.image_width, 0);

	JSAMPROW row_pointer[1];
	row_pointer[0] = &tmprowbuf[0];
	while (compress_.next_scanline < compress_.image_height) {
		unsigned i, j;
		unsigned offset = compress_.next_scanline * stride; //compress_.image_width * 2; //offset to the correct row
		for (i = 0, j = 0; i < compress_.image_width * 2; i += 4, j += 6) { //input strides by 4 bytes, output strides by 6 (2 pixels)
			tmprowbuf[j + 0] = input[offset + i + 0]; // Y (unique to this pixel)
			tmprowbuf[j + 1] = input[offset + i + 1]; // U (shared between pixels)
			tmprowbuf[j + 2] = input[offset + i + 3]; // V (shared between pixels)
			tmprowbuf[j + 3] = input[offset + i + 2]; // Y (unique to this pixel)
			tmprowbuf[j + 4] = input[offset + i + 1]; // U (shared between pixels)
			tmprowbuf[j + 5] = input[offset + i + 3]; // V (shared between pixels)
		}
		jpeg_write_scanlines(&compress_, row_pointer, 1);
	}
}

/*
 * Compress the incoming buffer from a supported NV format.
 * This naively unpacks the semi-planar NV12 to a YUV888 format for libjpeg.
 */
void EncoderLibJpeg::compressNV(const libcamera::MappedBuffer *frame)
{
	uint8_t tmprowbuf[compress_.image_width * 3];

	/*
	 * \todo Use the raw api, and only unpack the cb/cr samples to new line
	 * buffers. If possible, see if we can set appropriate pixel strides
	 * too to save even that copy.
	 *
	 * Possible hints at:
	 * https://sourceforge.net/p/libjpeg/mailman/message/30815123/
	 */
	unsigned int y_stride = pixelFormatInfo_->stride(compress_.image_width, 0);
	unsigned int c_stride = pixelFormatInfo_->stride(compress_.image_width, 1);

	unsigned int horzSubSample = 2 * compress_.image_width / c_stride;
	unsigned int vertSubSample = pixelFormatInfo_->planes[1].verticalSubSampling;

	unsigned int c_inc = horzSubSample == 1 ? 2 : 0;
	unsigned int cb_pos = nvSwap_ ? 1 : 0;
	unsigned int cr_pos = nvSwap_ ? 0 : 1;

	const unsigned char *src = static_cast<unsigned char *>(frame->maps()[0].data());
	const unsigned char *src_c = src + y_stride * compress_.image_height;

	JSAMPROW row_pointer[1];
	row_pointer[0] = &tmprowbuf[0];

	for (unsigned int y = 0; y < compress_.image_height; y++) {
		unsigned char *dst = &tmprowbuf[0];

		const unsigned char *src_y = src + y * compress_.image_width;
		const unsigned char *src_cb = src_c + (y / vertSubSample) * c_stride + cb_pos;
		const unsigned char *src_cr = src_c + (y / vertSubSample) * c_stride + cr_pos;

		for (unsigned int x = 0; x < compress_.image_width; x += 2) {
			dst[0] = *src_y;
			dst[1] = *src_cb;
			dst[2] = *src_cr;
			src_y++;
			src_cb += c_inc;
			src_cr += c_inc;
			dst += 3;

			dst[0] = *src_y;
			dst[1] = *src_cb;
			dst[2] = *src_cr;
			src_y++;
			src_cb += 2;
			src_cr += 2;
			dst += 3;
		}

		jpeg_write_scanlines(&compress_, row_pointer, 1);
	}
}

int EncoderLibJpeg::encode(const FrameBuffer *source,
			   const libcamera::Span<uint8_t> &dest)
{
	MappedFrameBuffer frame(source, PROT_READ);
	if (!frame.isValid()) {
		LOG(JPEG, Error) << "Failed to map FrameBuffer : "
				 << strerror(frame.error());
		return frame.error();
	}

	/*
	SET_RATIONAL(EXIF_IFD_EXIF, EXIF_TAG_FOCAL_LENGTH, numerator, denominator);
	SET_RATIONAL(EXIF_IFD_EXIF, EXIF_TAG_FNUMBER, numerator, denominator);
	SET_SHORT(EXIF_IFD_EXIF, EXIF_TAG_FLASH, flash);
	SET_SHORT(EXIF_IFD_EXIF, EXIF_TAG_WHITE_BALANCE, white_balance);
	SET_SHORT(EXIF_IFD_EXIF, EXIF_TAG_EXPOSURE_MODE, exposure_mode);
	SET_RATIONAL(EXIF_IFD_EXIF, EXIF_TAG_EXPOSURE_TIME, numerator, denominator);
	SET_STRING(EXIF_IFD_EXIF, EXIF_TAG_SUB_SEC_TIME, EXIF_FORMAT_ASCII, subsec_time);
	SET_STRING(EXIF_IFD_EXIF, EXIF_TAG_SUB_SEC_TIME_ORIGINAL, EXIF_FORMAT_ASCII, subsec_time);
	SET_STRING(EXIF_IFD_EXIF, EXIF_TAG_SUB_SEC_TIME_DIGITIZED, EXIF_FORMAT_ASCII, subsec_time);
	*/
	Exif exif;

	exif.setMake("Libcamera");
	exif.setModel("Kierans Camera");

	exif.setShort(EXIF_IFD_0, EXIF_TAG_IMAGE_WIDTH, compress_.image_width);
	exif.setLong(EXIF_IFD_EXIF, EXIF_TAG_PIXEL_X_DIMENSION, compress_.image_width);

	exif.setShort(EXIF_IFD_0, EXIF_TAG_IMAGE_LENGTH, compress_.image_height);
	exif.setLong(EXIF_IFD_EXIF, EXIF_TAG_PIXEL_Y_DIMENSION, compress_.image_height);

	exif.setShort(EXIF_IFD_0, EXIF_TAG_ORIENTATION, 1 /* default upright */);

	std::string now("Tue 28 Jul 14:35:47 BST 2020");
	exif.setString(EXIF_IFD_0, EXIF_TAG_DATE_TIME, EXIF_FORMAT_ASCII, now);
	exif.setString(EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_ORIGINAL, EXIF_FORMAT_ASCII, now);
	exif.setString(EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_DIGITIZED, EXIF_FORMAT_ASCII, now);

	Span<uint8_t> exif_data = exif.generate();

	unsigned char *destination = dest.data();
	unsigned long size = dest.size();

	/*
	 * The jpeg_mem_dest will reallocate if the required size is not
	 * sufficient. That means the output won't be written to the correct
	 * buffers.
	 *
	 * \todo Implement our own custom memory destination to prevent
	 * reallocation and prefer failure with correct reporting.
	 */
	jpeg_mem_dest(&compress_, &destination, &size);

	jpeg_start_compress(&compress_, TRUE);

	if (exif.size()) {
		/* Store Exif data in the JPEG_APP1 data block. */
		jpeg_write_marker(&compress_, JPEG_APP0 + 1,
				  static_cast<const JOCTET *>(exif_data.data()),
				  exif_data.size());
	}

	LOG(JPEG, Debug) << "JPEG Encode Starting:" << compress_.image_width
			 << "x" << compress_.image_height;

	if (nv_)
		compressNV(&frame);
	else if (compress_.in_color_space == JCS_YCbCr)
		compressYUV(&frame);
	else
		compressRGB(&frame);

	jpeg_finish_compress(&compress_);

	LOG(JPEG, Error) << "JPEG Compress Input size " << dest.size()
			 << " output size: " << size;

	if (destination != dest.data()) {
		LOG(JPEG, Error) << "JPEG REALLOCATED MEMORY "
				 << destination << " != " << dest.data();
	}

	return size;
}
