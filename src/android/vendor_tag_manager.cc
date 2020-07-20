/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include "vendor_tag_manager.h"

#include <vector>

//#include <base/strings/string_util.h>

//#include "cros-camera/common.h"

namespace cros {

VendorTagManager::VendorTagManager() {
  vendor_tag_ops_t::get_tag_count = VendorTagManager::get_tag_count;
  vendor_tag_ops_t::get_all_tags = VendorTagManager::get_all_tags;
  vendor_tag_ops_t::get_section_name = VendorTagManager::get_section_name;
  vendor_tag_ops_t::get_tag_name = VendorTagManager::get_tag_name;
  vendor_tag_ops_t::get_tag_type = VendorTagManager::get_tag_type;
}

int VendorTagManager::GetTagCount() const {
  return tags_.size();
}

void VendorTagManager::GetAllTags(uint32_t* tag_array) const {
  assert(tag_array != nullptr);
  uint32_t* ptr = tag_array;
  for (const auto& tag : tags_) {
    *ptr++ = tag.first;
  }
}

const char* VendorTagManager::GetSectionName(uint32_t tag) const {
  auto it = tags_.find(tag);
  if (it == tags_.end()) {
    return nullptr;
  }
  return it->second.section_name.c_str();
}

const char* VendorTagManager::GetTagName(uint32_t tag) const {
  auto it = tags_.find(tag);
  if (it == tags_.end()) {
    return nullptr;
  }
  return it->second.tag_name.c_str();
}

int VendorTagManager::GetTagType(uint32_t tag) const {
  auto it = tags_.find(tag);
  if (it == tags_.end()) {
    return -1;
  }
  return it->second.type;
}

bool VendorTagManager::Add(vendor_tag_ops_t* ops) {
  assert(ops != nullptr);
  assert(ops->get_tag_count != nullptr);
  int n = ops->get_tag_count(ops);
  std::vector<uint32_t> all_tags(n);
  ops->get_all_tags(ops, all_tags.data());
  for (uint32_t tag : all_tags) {
    const char* section_name = ops->get_section_name(ops, tag);
    const char* tag_name = ops->get_tag_name(ops, tag);
    int type = ops->get_tag_type(ops, tag);
    if (!Add(tag, section_name, tag_name, type)) {
      return false;
    }
  }
  return true;
}

bool VendorTagManager::Add(uint32_t tag,
                           const std::string& section_name,
                           const std::string& tag_name,
                           int type) {
  if (tag < CAMERA_METADATA_VENDOR_TAG_BOUNDARY ||
      tag >= kNextAvailableVendorTag) {
	  printf("BAKKA ERR 1\n");
    return false;
  }

  char buf[1024];

  sprintf(buf, "%s.%s", section_name.c_str(), tag_name.c_str());

  std::string full_name(buf);

  if (!full_names_.insert(full_name).second) {
	  printf("BAKKA ERR 2\n");
    return false;
  }

  TagInfo info = {section_name, tag_name, type};
  if (!tags_.emplace(tag, info).second) {
	  printf("BAKKA ERR 3\n");
    return false;
  }

  return true;
}

// static
int VendorTagManager::get_tag_count(const vendor_tag_ops_t* v) {
  auto* self = static_cast<const VendorTagManager*>(v);
  return self->GetTagCount();
}

// static
void VendorTagManager::get_all_tags(const vendor_tag_ops_t* v,
                                    uint32_t* tag_array) {
  auto* self = static_cast<const VendorTagManager*>(v);
  return self->GetAllTags(tag_array);
}

// static
const char* VendorTagManager::get_section_name(const vendor_tag_ops_t* v,
                                               uint32_t tag) {
  auto* self = static_cast<const VendorTagManager*>(v);
  return self->GetSectionName(tag);
}

// static
const char* VendorTagManager::get_tag_name(const vendor_tag_ops_t* v,
                                           uint32_t tag) {
  auto* self = static_cast<const VendorTagManager*>(v);
  return self->GetTagName(tag);
}

// static
int VendorTagManager::get_tag_type(const vendor_tag_ops_t* v, uint32_t tag) {
  auto* self = static_cast<const VendorTagManager*>(v);
  return self->GetTagType(tag);
}

}  // namespace cros
