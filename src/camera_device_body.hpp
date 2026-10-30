#pragma once

#include "camera_device_args.hpp"
#include "physics_body.hpp"
#include <memory>
#include <string>
#include <utility>

// Detached state only. It does not register a body or create resource links.
struct CameraDeviceBody {
  explicit CameraDeviceBody(std::shared_ptr<const physics::BodyProto> source)
      : prototype(std::move(source)) {}
  CameraDeviceBody(const CameraDeviceBody &) = delete;
  CameraDeviceBody &operator=(const CameraDeviceBody &) = delete;
  const std::shared_ptr<const physics::BodyProto> prototype;
  physics::Body body;
};

std::shared_ptr<const physics::BodyProto>
makeCameraDevicePrototype(std::string &error);

std::unique_ptr<CameraDeviceBody>
makeCameraDeviceBody(std::shared_ptr<const physics::BodyProto> prototype,
                     const CameraDeviceArgs &args, float creationTime,
                     std::string &error);
