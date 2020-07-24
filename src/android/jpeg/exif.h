/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * exif.h - EXIF tag creator and parser using libexif
 */
#ifndef __LIBCAMERA_EXIF_H__
#define __LIBCAMERA_EXIF_H__

#include <libexif/exif-data.h>

#include <libcamera/span.h>

#include <string>

class Exif
{
public:
	Exif();
	~Exif();

	int setShort(ExifIfd ifd, ExifTag tag, uint16_t item);
	int setLong(ExifIfd ifd, ExifTag tag, uint32_t item);
	int setString(ExifIfd ifd, ExifTag tag, ExifFormat format, const std::string &item);
	int setRational(ExifIfd ifd, ExifTag tag, uint32_t numerator, uint32_t denominator);

	int setMake(const std::string &make) { return setString(EXIF_IFD_0, EXIF_TAG_MODEL, EXIF_FORMAT_ASCII, make); }
	int setModel(const std::string &model) { return setString(EXIF_IFD_0, EXIF_TAG_MODEL, EXIF_FORMAT_ASCII, model); }

	libcamera::Span<uint8_t> generate();
	unsigned char *data() const { return exif_data_; }
	unsigned int size() const { return size_; }

private:
	ExifEntry *createEntry(ExifIfd ifd, ExifTag tag);
	ExifEntry *createEntry(ExifIfd ifd, ExifTag tag, ExifFormat format,
			       uint64_t components, unsigned int size);

	bool valid_;

	ExifData *data_;
	ExifMem *mem_;

	unsigned char *exif_data_;
	unsigned int size_;
};

#endif /* __LIBCAMERA_EXIF_H__ */
