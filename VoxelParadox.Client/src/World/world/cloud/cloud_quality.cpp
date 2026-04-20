#include "world/cloud/cloud_quality.hpp"

// 1. Standard Library
#include <algorithm>
#include <cctype>

namespace VoxelGame::Clouds {
namespace {

std::string normalized(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
  value.erase(std::remove(value.begin(), value.end(), '-'), value.end());
  value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
  return value;
}

} // namespace

const char* cloudQualityId(CloudQuality quality) {
  switch (quality) {
  case CloudQuality::LOW:
    return "low";
  case CloudQuality::MEDIUM:
    return "medium";
  case CloudQuality::HIGH:
    return "high";
  }
  return "medium";
}

const char* cloudQualityDisplayName(CloudQuality quality) {
  switch (quality) {
  case CloudQuality::LOW:
    return "Low";
  case CloudQuality::MEDIUM:
    return "Medium";
  case CloudQuality::HIGH:
    return "High";
  }
  return "Medium";
}

bool tryParseCloudQuality(const std::string& value, CloudQuality& outQuality) {
  const std::string key = normalized(value);
  if (key == "low") {
    outQuality = CloudQuality::LOW;
    return true;
  }
  if (key == "medium" || key == "normal") {
    outQuality = CloudQuality::MEDIUM;
    return true;
  }
  if (key == "high" || key == "ultra") {
    outQuality = CloudQuality::HIGH;
    return true;
  }
  return false;
}

} // namespace VoxelGame::Clouds
