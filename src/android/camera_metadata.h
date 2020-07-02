/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * camera_metadata.h - libcamera Android Camera Metadata Helper
 */
#ifndef __ANDROID_CAMERA_METADATA_H__
#define __ANDROID_CAMERA_METADATA_H__

#include <stdint.h>
#include <string>
#include <vector>

#include <system/camera_metadata.h>

class CameraMetadata
{
public:
	CameraMetadata(size_t entryCapacity, size_t dataCapacity);
	~CameraMetadata();

	bool isValid() const { return valid_; }
	bool addEntry(uint32_t tag, const void *data, size_t data_count);
	bool updateEntry(uint32_t tag, const void *data, size_t data_count);

	const std::vector<int32_t> &tags() { return tags_; }

	camera_metadata_t *get();
	const camera_metadata_t *get() const;

	size_t entries() const { return entries_; }
	size_t size() const { return size_; };

	std::string usage() const;

private:
	camera_metadata_t *metadata_;
	std::vector<int32_t> tags_;
	bool valid_;

	size_t entryCapacity_;
	size_t dataCapacity_;

	size_t entries_;
	size_t size_;
};

#endif /* __ANDROID_CAMERA_METADATA_H__ */
