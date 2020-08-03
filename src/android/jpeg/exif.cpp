/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * exif.cpp - EXIF tag creation and parser using libexif
 */

#include "exif.h"

#include "libcamera/internal/log.h"

using namespace libcamera;

LOG_DEFINE_CATEGORY(EXIF)

/*
* 0th IFD TIFF Tags from Intel HAL for reference.
#define EXIF_TAG_IMAGE_WIDTH                    0x0100
#define EXIF_TAG_IMAGE_HEIGHT                   0x0101
#define EXIF_TAG_IMAGE_DESCRIPTION              0x010e
#define EXIF_TAG_MAKE                           0x010f
#define EXIF_TAG_MODEL                          0x0110
#define EXIF_TAG_ORIENTATION                    0x0112
#define EXIF_TAG_X_RESOLUTION                   0x011A
#define EXIF_TAG_Y_RESOLUTION                   0x011B
#define EXIF_TAG_RESOLUTION_UNIT                0x0128
#define EXIF_TAG_SOFTWARE                       0x0131
#define EXIF_TAG_DATE_TIME                      0x0132
#define EXIF_TAG_YCBCR_POSITIONING              0x0213
#define EXIF_TAG_EXIF_IFD_POINTER               0x8769
#define EXIF_TAG_GPS_IFD_POINTER                0x8825

https://partnerissuetracker.corp.google.com/u/2/issues/161540086

# Exif tags we set for USB HAL:
SET_SHORT(EXIF_IFD_0, EXIF_TAG_IMAGE_WIDTH, width);
SET_LONG(EXIF_IFD_EXIF, EXIF_TAG_PIXEL_X_DIMENSION, width);
SET_SHORT(EXIF_IFD_0, EXIF_TAG_IMAGE_LENGTH, length);
SET_LONG(EXIF_IFD_EXIF, EXIF_TAG_PIXEL_Y_DIMENSION, length);
SET_STRING(EXIF_IFD_0, EXIF_TAG_DATE_TIME, EXIF_FORMAT_ASCII, buffer);
SET_STRING(EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_ORIGINAL, EXIF_FORMAT_ASCII, buffer);
SET_STRING(EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_DIGITIZED, EXIF_FORMAT_ASCII, buffer);
SET_RATIONAL(EXIF_IFD_EXIF, EXIF_TAG_FOCAL_LENGTH, numerator, denominator);
SET_SHORT(EXIF_IFD_0, EXIF_TAG_ORIENTATION, value);
SET_RATIONAL(EXIF_IFD_EXIF, EXIF_TAG_FNUMBER, numerator, denominator);
SET_SHORT(EXIF_IFD_EXIF, EXIF_TAG_FLASH, flash);
SET_SHORT(EXIF_IFD_EXIF, EXIF_TAG_WHITE_BALANCE, white_balance);
SET_SHORT(EXIF_IFD_EXIF, EXIF_TAG_EXPOSURE_MODE, exposure_mode);
SET_RATIONAL(EXIF_IFD_EXIF, EXIF_TAG_EXPOSURE_TIME, numerator, denominator);
SET_STRING(EXIF_IFD_EXIF, EXIF_TAG_SUB_SEC_TIME, EXIF_FORMAT_ASCII, subsec_time);
SET_STRING(EXIF_IFD_EXIF, EXIF_TAG_SUB_SEC_TIME_ORIGINAL, EXIF_FORMAT_ASCII, subsec_time);
SET_STRING(EXIF_IFD_EXIF, EXIF_TAG_SUB_SEC_TIME_DIGITIZED, EXIF_FORMAT_ASCII, subsec_time);

https://libexif.github.io/
https://stackoverflow.com/questions/48077371/use-libexif-with-libjpeg-to-set-exif-tags-on-an-existing-jpeg
https://libexif.github.io/api/index.html
https://github.com/libexif/libexif/blob/master/contrib/examples/write-exif.c
https://fossies.org/linux/libexif/contrib/examples/write-exif.c
https://dev.exiv2.org/projects/exiv2/wiki/The_Metadata_in_JPEG_files

*/

Exif::Exif()
	: valid_(false), exif_data_(0), size_(0)
{
	/* Create an ExifMem allocator to construct entries. */
	mem_ = exif_mem_new_default();
	if (!mem_) {
		LOG(EXIF, Fatal) << "Failed to allocate ExifMem Allocator";
		return;
	}

	data_ = exif_data_new_mem(mem_);
	if (!data_) {
		LOG(EXIF, Fatal) << "Failed to allocate an ExifData structure";
		return;
	}

	valid_ = true;

	exif_data_set_option(data_, EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
	exif_data_set_data_type(data_, EXIF_DATA_TYPE_COMPRESSED);

	/*
	 * Big-Endian: EXIF_BYTE_ORDER_MOTOROLA
	 * Little Endian: EXIF_BYTE_ORDER_INTEL
	 */
	exif_data_set_byte_order(data_, EXIF_BYTE_ORDER_INTEL);

	// Set exif version to 2.2.
	//if (!SetExifVersion("0220")) {
	//}

	/* Create the mandatory EXIF fields with default data */
	exif_data_fix(data_);
}

Exif::~Exif()
{
	if (exif_data_)
		free(exif_data_);

	if (data_)
		exif_data_unref(data_);

	if (mem_)
		exif_mem_unref(mem_);
}

ExifEntry *Exif::createEntry(ExifIfd ifd, ExifTag tag)
{
	ExifContent *content = data_->ifd[ifd];
	ExifEntry *entry = exif_content_get_entry(content, tag);

	if (entry) {
		exif_entry_ref(entry);
		return entry;
	}

	entry = exif_entry_new_mem(mem_);
	if (!entry) {
		LOG(EXIF, Fatal) << "Failed to allocated new entry";
		return nullptr;
	}

	entry->tag = tag;

	exif_content_add_entry(content, entry);
	exif_entry_initialize(entry, tag);

	return entry;
}

ExifEntry *Exif::createEntry(ExifIfd ifd, ExifTag tag, ExifFormat format,
			     uint64_t components, unsigned int size)
{
	ExifContent *content = data_->ifd[ifd];

	/* Replace any existing entry with the same tag. */
	ExifEntry *existing = exif_content_get_entry(content, tag);
	exif_content_remove_entry(content, existing);

	ExifEntry *entry = exif_entry_new_mem(mem_);
	if (!entry) {
		LOG(EXIF, Fatal) << "Failed to allocated new entry";
		return nullptr;
	}

	void *buffer = exif_mem_alloc(mem_, size);
	if (!buffer) {
		LOG(EXIF, Fatal) << "Failed to allocate buffer for variable entry";
		exif_mem_unref(mem_);
		return nullptr;
	}

	entry->data = static_cast<unsigned char *>(buffer);
	entry->components = components;
	entry->format = format;
	entry->size = size;
	entry->tag = tag;

	exif_content_add_entry(content, entry);

	return entry;
}

int Exif::setShort(ExifIfd ifd, ExifTag tag, uint16_t item)
{
	ExifEntry *entry = createEntry(ifd, tag);

	exif_set_short(entry->data, EXIF_BYTE_ORDER_INTEL, item);
	exif_entry_unref(entry);

	return 0;
}

int Exif::setLong(ExifIfd ifd, ExifTag tag, uint32_t item)
{
	ExifEntry *entry = createEntry(ifd, tag);

	exif_set_short(entry->data, EXIF_BYTE_ORDER_INTEL, item);
	exif_entry_unref(entry);

	return 0;
}

int Exif::setRational(ExifIfd ifd, ExifTag tag, uint32_t numerator, uint32_t denominator)
{
	ExifEntry *entry = createEntry(ifd, tag);
	ExifRational item{ numerator, denominator };

	exif_set_rational(entry->data, EXIF_BYTE_ORDER_INTEL, item);
	exif_entry_unref(entry);

	return 0;
}

int Exif::setString(ExifIfd ifd, ExifTag tag, ExifFormat format, const std::string &item)
{
	size_t length = item.length();

	ExifEntry *entry = createEntry(ifd, tag, format, length, length);
	if (!entry) {
		LOG(EXIF, Error) << "Failed to add tag: " << tag;
		return -ENOMEM;
	}

	memcpy(entry->data, item.c_str(), length);
	exif_entry_unref(entry);

	return 0;
}

Span<uint8_t> Exif::generate()
{
	if (exif_data_) {
		free(exif_data_);
		exif_data_ = nullptr;
	}

	exif_data_save_data(data_, &exif_data_, &size_);

	LOG(EXIF, Debug) << "Created EXIF instance (" << size_ << " bytes)";

	return { exif_data_, size_ };
}

