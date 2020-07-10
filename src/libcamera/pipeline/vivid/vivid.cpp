/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * vivid.cpp - Pipeline handler for the vivid capture device
 */

#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/log.h"
#include "libcamera/internal/pipeline_handler.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(VIVID)

class PipelineHandlerVivid : public PipelineHandler
{
public:
	PipelineHandlerVivid(CameraManager *manager);

	CameraConfiguration *generateConfiguration(Camera *camera,
						   const StreamRoles &roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera) override;
	void stop(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	bool match(DeviceEnumerator *enumerator) override;
};

PipelineHandlerVivid::PipelineHandlerVivid(CameraManager *manager)
	: PipelineHandler(manager)
{
}

CameraConfiguration *PipelineHandlerVivid::generateConfiguration(Camera *camera,
								 const StreamRoles &roles)
{
	return nullptr;
}

int PipelineHandlerVivid::configure(Camera *camera, CameraConfiguration *config)
{
	return -1;
}

int PipelineHandlerVivid::exportFrameBuffers(Camera *camera, Stream *stream,
					     std::vector<std::unique_ptr<FrameBuffer>> *buffers)
{
	return -1;
}

int PipelineHandlerVivid::start(Camera *camera)
{
	return -1;
}

void PipelineHandlerVivid::stop(Camera *camera)
{
}

int PipelineHandlerVivid::queueRequestDevice(Camera *camera, Request *request)
{
	return -1;
}

bool PipelineHandlerVivid::match(DeviceEnumerator *enumerator)
{
	DeviceMatch dm("vivid");
	dm.add("vivid-000-vid-cap");

	MediaDevice *media = acquireMediaDevice(enumerator, dm);
	if (!media)
		return false;

	LOG(VIVID, Debug) << "Obtained Vivid Device";

	return false; // Prevent infinite loops for now
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerVivid);

} /* namespace libcamera */
